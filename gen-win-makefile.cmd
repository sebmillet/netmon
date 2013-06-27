@REM
@REM gen-win-makefiles.cmd
@REM
@REM Update the four variables below according to your environment
@REM Sébastien Millet, 2010-2013
@REM

@set DIR_INCLUDE_OPENSSL="C:\openssl\include"
@set DIR_LIB_OPENSSL="C:\openssl\lib"
@set DIR_INCLUDE_PTHREADS="C:\pthreads"
@set DIR_LIB_PTHREADS="C:\pthreads"

@echo openssl lib directory:      %DIR_LIB_OPENSSL%
@echo openssl include directory:  %DIR_INCLUDE_OPENSSL%
@echo pthreads lib directory:     %DIR_LIB_PTHREADS%
@echo pthreads include directory: %DIR_INCLUDE_PTHREADS%
@echo .
@echo Please make sure the above corresponds to where you compiled static
@echo libs and put include files, if not, update this file (gen-win-makefile.cmd)
@echo accordingly.
@pause

bakefile -f mingw netmon.bkl -DINCOPENSSL=%DIR_INCLUDE_OPENSSL% -DINCPTHREADS=%DIR_INCLUDE_PTHREADS% -DLIBOPENSSL=%DIR_LIB_OPENSSL% -DLIBPTHREADS=%DIR_LIB_PTHREADS%

