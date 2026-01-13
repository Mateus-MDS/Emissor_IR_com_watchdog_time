# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Incluir Pico SDK
include(pico_sdk_import.cmake)

project(Teste_protocolo C CXX ASM)

# Inicializar SDK
pico_sdk_init()

# Executável principal
add_executable(Teste_protocolo
    Teste_protocolo.c
    lib/custom_ir.c
    lib/ssd1306.c
)

# Configurar nome e versão
pico_set_program_name(Teste_protocolo "Teste_protocolo")
pico_set_program_version(Teste_protocolo "1.0")

# Habilitar USB para debug
pico_enable_stdio_usb(Teste_protocolo 1)
pico_enable_stdio_uart(Teste_protocolo 0)


# Linkar bibliotecas necessárias
target_link_libraries(Teste_protocolo
    pico_stdlib
    hardware_pwm
    hardware_dma
    hardware_gpio
    hardware_i2c
)

# Incluir diretórios
target_include_directories(Teste_protocolo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Gerar arquivos UF2
pico_add_extra_outputs(Teste_protocolo)