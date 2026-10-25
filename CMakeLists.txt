# 文件路径：项目根目录/CMakeLists.txt

# 1. 指定 CMake 最低版本（必须放在最前面）
cmake_minimum_required(VERSION 3.21)

# 2. 定义项目名称、版本和语言
project(SoftRasterizer VERSION 1.0.0 LANGUAGES CXX)

# 3. 设置 C++ 标准为 C++17，并强制要求使用该标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3.1 源文件是 UTF-8 编码，而 MSVC 默认按本地代码页（简体中文系统为 GBK）解析源文件，
#     遇到中文注释可能报 C4819 甚至误解析行尾，因此显式指定 UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 4. 图像与帧缓冲相关的基础模块（TGA 读写）编译成静态库
add_library(softrasterizer_core STATIC
    include/tgaimage.cpp      # TGA 图像读写实现
)

# 5. 为库目标指定公开头文件路径
#    PUBLIC 表示使用 softrasterizer_core 的目标也能访问该路径
target_include_directories(softrasterizer_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# 6. 添加可执行文件目标
add_executable(SoftRasterizer
    src/main.cpp           # 程序入口
    src/model.cpp
)

# 7. 为可执行文件指定私有头文件路径
#    后续的几何处理、光栅化等模块头文件放在 src 下
target_include_directories(SoftRasterizer PRIVATE ${PROJECT_SOURCE_DIR}/src)

# 8. 将 softrasterizer_core 链接到 SoftRasterizer
target_link_libraries(SoftRasterizer PRIVATE softrasterizer_core)

# 8.1 静态链接 GCC 运行时（libstdc++ / libgcc / winpthread）
#     PATH 里 D:\Git\mingw64\bin 排在 C:\MinGW-w64\bin 之前，动态链接时程序会先加载
#     Git 自带的新版 DLL，与本机 GCC 8.1 编译的二进制不兼容，调试器启动即报
#     0xc0000139 (STATUS_ENTRYPOINT_NOT_FOUND)。静态链接后 exe 不再依赖这些 DLL。
if(MINGW)
    target_link_options(SoftRasterizer PRIVATE -static)
endif()

# 9. 固定可执行文件输出位置为 build/bin
#    多配置生成器（Visual Studio）默认会再加一层 Debug/Release 子目录，
#    这里用配置名后缀属性覆盖掉，使单/多配置生成器的产物路径保持一致，
#    便于 .vscode/launch.json 稳定引用
set_target_properties(SoftRasterizer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

# 10. 设置安装规则
install(TARGETS SoftRasterizer RUNTIME DESTINATION bin)
install(TARGETS softrasterizer_core ARCHIVE DESTINATION lib)