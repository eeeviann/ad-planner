@echo off
chcp 65001 >nul
echo ========================================
echo   广告投放智能规划Agent
echo   基于整数规划求解器 + 大语言模型
echo ========================================
echo.
echo 输入自然语言描述需求，自动求解
echo 输入 quit 退出
echo.

:set_api_key
if "%SILICONFLOW_KEY%"=="" (
    echo [提示] 未设置SILICONFLOW_KEY环境变量
    echo 请先设置: set SILICONFLOW_KEY=sk-your-key
    echo 或在系统环境变量中添加
    echo.
    echo 使用本地演示模式（预设参数）...
    echo.
    goto :local_demo
)

:loop
echo.
set /p user_input="> "
if "%user_input%"=="quit" goto :end
if "%user_input%"=="exit" goto :end

:: 构建API请求
echo 正在调用LLM理解需求...

:: 调用SiliconFlow API
curl -s -X POST "https://api.siliconflow.cn/v1/chat/completions" ^
  -H "Authorization: Bearer %SILICONFLOW_KEY%" ^
  -H "Content-Type: application/json" ^
  -d "{
    \"model\": \"deepseek-ai/DeepSeek-V3\",
    \"messages\": [
      {
        \"role\": \"system\",
        \"content\": \"你是一个广告投放规划助手。用户描述需求时，提取关键参数并输出JSON。只输出JSON，不要其他内容。参数范围：plan_budget（策划预算，默认100），tv_cost（TV广告单价，默认30）。示例：用户说'策划预算是200万'，输出{\"plan_budget\":200}。用户只是打招呼时输出{\"none\":true}\"
      },
      {
        \"role\": \"user\",
        \"content\": \"%user_input%\"
      }
    ],
    \"temperature\": 0.1,
    \"max_tokens\": 100
  }" > %TEMP%\agent_response.json

:: 提取JSON中的content字段（简化处理）
findstr /C:"content" %TEMP%\agent_response.json | findstr /V "role" > %TEMP%\agent_content.txt

set /p llm_output=<%TEMP%\agent_content.txt

:: 简单参数解析
echo LLM回复: %llm_output%

:: 判断问题类型
echo.
if not "%llm_output%"=="" (
    if not "%llm_output%"=="{}" (
        echo 调用求解器...
        if "%llm_output:~-7,-1%"=="200" (
            src\ip_solver --q3
        ) else if "%llm_output%"=="{}" (
            echo 请描述具体的广告投放需求
        ) else (
            src\ip_solver --q1
        )
    )
)

goto :loop

:local_demo
echo 【本地演示】输入以下数字选择问题：
echo   1 - 第1问（策划预算100万）
echo   2 - 第2问（TV广告25万）
echo   3 - 第3问（策划预算200万）
echo.

:local_loop
set /p choice="> "
if "%choice%"=="1" src\ip_solver --q1 & goto :local_loop
if "%choice%"=="2" src\ip_solver --q2 & goto :local_loop
if "%choice%"=="3" src\ip_solver --q3 & goto :local_loop
if "%choice%"=="quit" goto :end
echo 无效选择
goto :local_loop

:end
del %TEMP%\agent_response.json 2>nul
del %TEMP%\agent_content.txt 2>nul
echo 再见！
