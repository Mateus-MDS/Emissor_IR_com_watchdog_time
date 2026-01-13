# Controle de Ar-Condicionado via IR com Watchdog Timer (RP2040)

Este repositório apresenta a implementação de um **sistema embarcado robusto** para **controle de ar-condicionado via infravermelho (IR)** utilizando a **Raspberry Pi Pico / BitDogLab (RP2040)**, integrado a um **Watchdog Timer (WDT)** para garantir **recuperação automática em caso de falhas de software**.

O projeto foi desenvolvido como parte da atividade **“Uso do Watchdog Timer (WDT) no RP2040”**, pertencente ao programa **EmbarcaTech – Desenvolvimento de Sensores e Atuadores IoT (Parte 11)**.

---

## 🎯 Objetivo do Projeto

Demonstrar, de forma prática e aplicada, o uso do **Watchdog Timer (WDT)** em um sistema embarcado real, integrando:

- Controle de ar-condicionado via IR  
- Interface com display OLED  
- Botões físicos e comandos via UART  
- Detecção e simulação de falhas de software  
- Recuperação automática do sistema  
- Diagnóstico detalhado da causa do reset  

---

## 🧠 Conceitos Aplicados

- Watchdog Timer como temporizador de segurança  
- Alimentação estratégica do watchdog (*watchdog_update()*)  
- Simulação de falhas por **loops infinitos**  
- Uso de **Scratch Registers** do RP2040  
- Interface visual para depuração (OLED + LEDs)  
- Boas práticas de confiabilidade em sistemas embarcados  

---

## 🛠️ Hardware Utilizado

- Raspberry Pi Pico / BitDogLab (RP2040)  
- Display OLED SSD1306 (I2C)  
- LEDs indicadores (boot, operação e falha)  
- Botões físicos (A e B)  
- LED infravermelho (IR)  

---

## 🔌 Mapeamento de Pinos

| Função | GPIO |
|------|------|
| LED Boot (vermelho) | 13 |
| LED Operação (verde) | 11 |
| LED Falha (azul) | 12 |
| Botão A (falha proposital) | 5 |
| Botão B (comandos IR) | 6 |
| LED IR | 16 |
| LED onboard | 25 |
| I2C SDA (OLED) | 14 |
| I2C SCL (OLED) | 15 |

---

## 🚨 Simulação de Falhas

  O sistema implementa duas falhas intencionais, utilizadas para validar o funcionamento do Watchdog.

---

## 🔴 Falha 1 – Botão A

Ao pressionar o Botão A, o sistema entra em um loop infinito, deixando de alimentar o Watchdog.

- #define FALHA_BOTAO_A 0x01

### Resultado:
- Sistema trava propositalmente
- Watchdog não é alimentado
- Reset automático após o timeout

---

## 🔴 Falha 2 – Comando IR (22°C)

Ao tentar configurar o ar-condicionado para 22°C, o sistema simula um erro de software.

- #define FALHA_TEMP_22C 0x02

### Resultado:
- Loop infinito sem watchdog_update()
- Reset automático pelo Watchdog

---

## 🔍 Diagnóstico Pós-Reset

### Após cada reinicialização, o sistema:

- Verifica se o reset foi causado pelo Watchdog
- Incrementa um contador de resets
- Registra o código da última falha

Essas informações são armazenadas nos Scratch Registers do RP2040 e exibidas no display OLED durante o boot.

### Informações exibidas no boot:

- Tipo de reset (normal ou watchdog)
- Quantidade de resets por WDT
- Código da falha
- Timeout configurado

--- 

## 📟 Interface de Usuário

### Display OLED
- Diagnóstico de boot
- Estado atual do ar-condicionado
- Indicação de falha induzida

### LEDs
- 🔴 Vermelho: boot/reset
- 🟢 Verde: operação normal
- 🔵 Azul: falha/travamento

## Vídeo Demonstrativo

Click [AQUI](https://www.youtube.com/watch?v=s4NObRXN48I&feature=youtu.be) para acessar o link do Vídeo Ensaio


