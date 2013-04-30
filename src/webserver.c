/* Correction du TP de programmation système UNIX

   TP serveur HTTP multi-thread

   Question 3

   Réponse basée sur la question 1
   Les modifications sont indiquées par XXX

   -
   Antoine Miné
   06/05/2007
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>


/* affiche un message d'erreur, errno et quitte */
void fatal_error(const char* msg)
{
  fprintf(stderr, "%s (errno: %s)\n", msg, strerror(errno));
  exit(1);
}


/* XXX structure de données pour les pool de threads */

#define NB 5     /* nombre total de threads */

/* variables partagées */
volatile int nb_endormies;      /* nombre de threads endormies */
volatile int client;            /* connection reçue en attente de traitement */

pthread_mutex_t mutex;          /* protège les variables partagées */
pthread_cond_t  nb_changed;     /* signal que 'nb' a été modifié */
pthread_cond_t  client_changed; /* signal que 'client' a été modifié */



/* crée une socket d'écoute sur le port indiqué 
   retourne son descripteur 
*/
int cree_socket_ecoute(int port)
{
  int listen_fd;           /* socket */
  struct sockaddr_in addr; /* adresse IPv4 d'écoute */
  int one = 1;             /* utilisé avec setsockopt */
  
  /* crée une socket TCP */
  listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (listen_fd==-1) fatal_error("échec de socket");
  
  /* évite le délai entre deux bind successifs sur le même port */
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))==-1)
    fatal_error("échec de setsockopt(SO_REUSEADDR)");

  /* remplit l'adresse addr */
  memset(&addr, 0, sizeof(addr));  /* initialisation à 0 */
  addr.sin_family = AF_INET;       /* adresse Internet */
  addr.sin_port   = htons(port);   /* port: 2 octets en ordre de réseau */
  /* on accepte les connections sur toutes les adresses IP de la machine */
  addr.sin_addr.s_addr = htonl(INADDR_ANY); 

  /* ancre listen_fd à l'adresse addr */
  if (bind(listen_fd, (const struct sockaddr*) &addr, sizeof(addr))==-1)
    fatal_error("échec de bind");

  /* transforme liste_fd en une socket d'écoute */
  if (listen(listen_fd, 15)==-1) 
    fatal_error("échec de listen");
  
  fprintf(stderr, "Serveur actif sur le port %i\n", port);

  return listen_fd;
}

/* lit une ligne de la forme 
     GET /chemin HTTP/1.1
   et stocke 'chemin' dans le buffer url de taille size
   XXX on renvoie -1 en cas d'erreur, sans fermer la connection ni la thread
*/
int lit_get(FILE* stream, char* url, int size)
{
  char buf[4096];
  int i,j;
  
  /* lit la requête */
  if (!fgets(buf, sizeof(buf), stream)) return -1;
  
  /* extrait l'URL et stocke-la dans url */
  if (strncmp(buf, "GET", 3)) return -1;
  for (i=3; buf[i]==' '; i++);
  if (!strncmp(buf+i, "http://", 7)) i+=7;
  for (; buf[i] && buf[i]!='/'; i++);
  if (buf[i]=='/') i++;
  for (j=0; buf[i] && buf[i]!=' ' && j<size-1; j++, i++) url[j] = buf[i];
  url[j] = 0;
  for (; buf[i]==' '; i++);
  if (strncmp(buf+i, "HTTP/1.1", 8)) return -1;

  return 0;
}

/* lit les en-têtes 
   renvoie 0 si l'en-tête 'connection: close' a été trouvée, 
   1 sinon
   XXX on renvoie -1 en cas d'erreur, sans fermer la connection ni la thread
 */
int lit_en_tetes(FILE* stream)
{
  char buf[4096];
  int keepalive = 1;

  while (1) {
    if (!fgets(buf, sizeof(buf), stream)) return -1;
    
    /* fin des en-têtes */
    if (buf[0]=='\n' || buf[0]=='\r') break;
    
    /* détecte l'en-tête 'Connection: close' */
    if (!strncasecmp(buf, "Connection:", 11) ||
	strstr(buf, "close"))
      keepalive = 0;
  }
  
  return keepalive;
}

/* envoie une erreur 404 
   XXX on renvoie -1, sans fermer la connection ni la thread   
 */
int envoie_404(FILE* stream, char* url)
{
  fprintf(stream, "HTTP/1.1 404 Not found\r\n");
  fprintf(stream, "Connection: close\r\n");
  fprintf(stream, "Content-type: text/html\r\n");
  fprintf(stream, "\r\n");
  fprintf(stream, 
	  "<html><head><title>Not Found</title></head>"
	  "<body><p>Sorry, the object you requested was not found: "
	  "<tt>/%s</tt>.</body></html>\r\n", 
	  url);
  return -1;
}

/* devine le type MIME d'un fichier */
char* type_fichier(char* chemin)
{
  int len = strlen(chemin);
  if (!strcasecmp(chemin+len-5, ".html") ||
      !strcasecmp(chemin+len-4, ".htm")) return "text/html";
  if (!strcasecmp(chemin+len-4, ".css")) return "text/css";
  if (!strcasecmp(chemin+len-4, ".png")) return "image/png";
  if (!strcasecmp(chemin+len-4, ".gif")) return "image/gif";
  if (!strcasecmp(chemin+len-5, ".jpeg") ||
      !strcasecmp(chemin+len-4, ".jpg")) return "image/jpeg";
  return "text/ascii";
}

/* envoie la contenu du fichier
   XXX on renvoie -1 en cas d'erreur, sans fermer la connection ni la thread
*/
int envoie_fichier(FILE* stream, char* chemin, int keepalive)
{
  char modiftime[30];
  char curtime[30];
  struct timeval  tv;
  char buf[4096];
  struct stat s;
  int fd;

  /* pour des raisons de sécurité, on évite les chemins contenant .. */
  if (strstr(chemin,"..")) return envoie_404(stream, chemin);

  /* ouverture et vérifications */
  fd = open(chemin, O_RDONLY);
  if (fd==-1) return envoie_404(stream, chemin);
  if (fstat(fd, &s)==-1 || !S_ISREG(s.st_mode) || !(s.st_mode & S_IROTH))
    { close(fd); return envoie_404(stream, chemin); }

  /* calcul des dates */
  if (gettimeofday(&tv, NULL) ||
      !ctime_r(&s.st_mtime, modiftime) ||
      !ctime_r(&tv.tv_sec, curtime))
    { close(fd); return envoie_404(stream, chemin); }
  modiftime[strlen(modiftime)-1] = 0; /* supprime le \n final */
  curtime[strlen(curtime)-1] = 0;     /* supprime le \n final */

  /* envoie l'en-tête */
  fprintf(stream, "HTTP/1.1 200 OK\r\n");
  fprintf(stream, "Connection: %s\r\n", keepalive ? "keep-alive" : "close");
  fprintf(stream, "Content-length: %li\r\n", (long)s.st_size);
  fprintf(stream, "Content-type: %s\r\n", type_fichier(chemin));
  fprintf(stream, "Date: %s\r\n", curtime);
  fprintf(stream, "Last-modified: %s\r\n", modiftime);
  fprintf(stream, "\r\n");

  /* envoie le corps */
  while (1) {
    int r = read(fd, buf, sizeof(buf)), w;
    if (r==0) break;
    if (r<0) {
      if (errno==EINTR) continue;
      close(fd);
      return -1;
    }
    for (w=0; w<r; ) {
      int a = fwrite(buf+w, 1, r-w, stream);
      if (a<=0) {
	if (errno==EINTR) continue;
	close(fd);
	return -1;
      }
      w += a;
    }
  }

  close(fd);
  return 0;
}

/* XXX
   traitement d'une connection 
   lancé dans une thread séparée par main
   la thread ne se termine qu'avec le serveur
 */
void* traite_connection(void* arg)
{
  FILE* stream;
  char url[4096];
  int keepalive;
 
  errno = pthread_mutex_lock(&mutex);
  if (errno) fatal_error("échec de pthread_mutex_lock");

  while (1) {

    /* attend une nouvelle socket de la part de boucle */
    while (client==-1) {
      errno = pthread_cond_wait(&client_changed, &mutex);  
      if (errno) fatal_error("échec de pthread_cond_wait");
    }

    /* récupère client */
    stream = fdopen(client, "r+");
    setlinebuf(stream);
    client = -1;
    nb_endormies--;

    printf("%li début de connection\n", pthread_self());

    errno = pthread_mutex_unlock(&mutex);
    if (errno) fatal_error("échec de pthread_mutex_unlock");
 
    /* boucle de traitement des requêtes */
    do {
      /* lit la requête */
      if (lit_get(stream, url, sizeof(url)) == -1) break;
      if ((keepalive = lit_en_tetes(stream)) == -1) break;
      
      /* envoie la réponse */
      printf("%li requête %s\n", pthread_self(), url);
      if (envoie_fichier(stream, url, keepalive) == -1) break;
    }
    while (keepalive);
    
    /* fin de connection */
    printf("%li fin de connection\n", pthread_self());
    fclose(stream);

    /* signale que la thread est libre */
    errno = pthread_mutex_lock(&mutex);
    if (errno) fatal_error("échec de pthread_mutex_lock");
    nb_endormies++; 
    errno = pthread_cond_signal(&nb_changed);
    if (errno) fatal_error("échec de pthread_cond_signal");
  }
}


/* boucle de réception des connections */
void boucle(int listen_fd)
{
  errno = pthread_mutex_lock(&mutex);
  if (errno) fatal_error("échec de pthread_mutex_lock");

  while (1) {
    int x;

    /* XXX attend qu'une thread se réveille */
    while (!nb_endormies) {
      errno = pthread_cond_wait(&nb_changed, &mutex);
      if (errno) fatal_error("échec de pthread_cond_wait");
    }
    
    /* attend une connection */
    errno = pthread_mutex_unlock(&mutex);
    if (errno) fatal_error("échec de pthread_mutex_unlock");
    x = accept(listen_fd, NULL, 0);

    /* signale la connection à une thread */
    errno = pthread_mutex_lock(&mutex);
    if (errno) fatal_error("échec de pthread_mutex_lock");
    client = x;   
    if (client==-1) {
      if (errno==EINTR || errno==ECONNABORTED) continue; /* non fatal */
      fatal_error("échec de accept");
    }
    errno = pthread_cond_signal(&client_changed);
    if (errno) fatal_error("échec de pthread_cond_signal");
  }
}

int main()
{
  pthread_t id;
  int i;

  /* XXX initialise le pool de thread */
  nb_endormies = NB;  
  client = -1;
  errno = pthread_mutex_init(&mutex, NULL);
  if (errno) fatal_error("échec de pthread_mutex_init");
  errno = pthread_cond_init(&nb_changed, NULL);
  if (errno) fatal_error("échec de pthread_cond_init");
  errno = pthread_cond_init(&client_changed, NULL);
  if (errno) fatal_error("échec de pthread_cond_init");
  for (i=0; i<NB; i++)  {
    errno = pthread_create(&id, NULL, traite_connection, NULL);
    if (errno) fatal_error("échec de pthread_create");
  }

  /* supprime SIGPIPE */
  signal(SIGPIPE, SIG_IGN);

  /* lance le serveur */
  boucle(cree_socket_ecoute(8080));

  return 0;
}
