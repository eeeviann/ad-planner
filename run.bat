@echo off
chcp 65001 >nul
setlocal

rem =====================================================================
rem  广告投放整数规划案例 · Windows 启动脚本
rem
rem  注意：本脚本对应的是「广告投放案例」（src/ip_solver.cpp），
rem  与仓库主线「通用自然语言整数规划 Agent」（ad-planner）是两套代码。
rem  主线用法见 README，直接运行 ad-planner.exe 即可。
rem
rem  本文件保存为 UTF-8 编码。若中文显示为乱码，请把本文件另存为
rem  「ANSI / GBK」编码后再运行。
rem =====================================================================

cd /d "%~dp0"

echo ========================================
echo   广告投放整数规划案例
echo   （优格公司早餐麦片推广问题）
echo ========================================
echo.

rem 兼容两种环境变量名
if not "%SILICONFLOW_KEY%"=="" set "SILICONFLOW_API_KEY=%SILICONFLOW_KEY%"

if not exist "ip_solver.exe" (
    echo [错误] 找不到 ip_solver.exe，请先编译：
    echo     make
    echo 或  g++ -std=c++17 -Wall -O2 -o ip_solver.exe src/ip_solver.cpp
    echo.
    pause
    exit /b 1
)

echo 输入数字选择要解决的问题：
echo   1 - 第 1 问（策划预算 100 万）
echo   2 - 第 2 问（电视广告单价 25 万）
echo   3 - 第 3 问（策划预算 200 万）
echo   q - 退出
echo.

:local_loop
set "choice="
set /p choice="> "

if /i "%choice%"=="q"     goto :end
if /i "%choice%"=="quit"  goto :end
if "%choice%"=="1" ip_solver.exe --q1 & goto :local_loop
if "%choice%"=="2" ip_solver.exe --q2 & goto :local_loop
if "%choice%"=="3" ip_solver.exe --q3 & goto :local_loop

echo 无效选择，请输入 1 / 2 / 3 / q
goto :local_loop

:end
echo 再见！
endlocal
