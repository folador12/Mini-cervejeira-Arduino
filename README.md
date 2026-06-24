# 🍺 Mini Cervejeira — Arduino + Supervisório Node-RED

Projeto de uma **mini cervejeira** controlada por **Arduino (atuando como CLP)** e
monitorada por um **supervisório web** feito em **Node-RED + Dashboard 2.0**.

O Arduino controla aquecedor, bomba e sensor de temperatura; o supervisório mostra
temperatura, etapa do processo e a **curva de mostura (Alvo × Real)**, e envia comandos
de volta. Há um **modo de simulação** que roda o painel sem o hardware — e que fala
exatamente o mesmo protocolo do Arduino real.

![Supervisório — Pilsen](docs/supervisorio_pilsen.png)

## 📂 Estrutura

```
.
├── README.md                  → este arquivo
├── firmware/
│   └── mini_cervejeira_v6/
│       └── mini_cervejeira_v6.ino   → código do Arduino (CLP)
├── node-red/
│   ├── cervejeira_nodered_SIMULACAO.json → supervisório v2 (Dashboard 2.0 + simulação)
│   └── cervejeira_nodered.json           → supervisório v1 (Dashboard 1.0, legado)
└── docs/
    ├── supervisorio_inicial.png
    ├── supervisorio_teste.png
    └── supervisorio_pilsen.png
```

## ✨ O que o supervisório v2 tem

- Visual repaginado (Dashboard 2.0, tema escuro estilo cerveja).
- **Card de status**: temperatura grande, alvo, etapa, cronômetro e chips de Bomba/Aquecedor.
- **Gráfico de mostura "Alvo × Real"** — acompanha a curva de temperatura do processo.
- **Receitas / modos** selecionáveis:
  - **Teste / Padrão** (65 °C · 2 min) — *idêntico ao firmware atual do Arduino*.
  - **Pilsen** — mostura por etapas 52 → 63 → 78 °C.
  - **American Pale Ale** — 67 → 78 °C.
- Motor de simulação que reproduz a lógica do `.ino` (histerese ±0,5 °C, intertravamento
  da bomba, troca automática de etapa) e emite o **mesmo JSON** do Arduino.
- **Plug-and-play:** detecta a porta do Arduino sozinho e alterna simulação ↔ hardware
  automaticamente — sem habilitar/desabilitar nós nem configurar porta COM.

| Inicial | Rodando (Teste) | Pilsen (etapas) |
|---|---|---|
| ![](docs/supervisorio_inicial.png) | ![](docs/supervisorio_teste.png) | ![](docs/supervisorio_pilsen.png) |

## 🚀 Rodar o supervisório (simulação, sem Arduino)

Instalar uma vez:

```bash
npm install -g node-red
# na pasta do Node-RED do usuário (ex.: C:\Users\<voce>\.node-red):
npm install @flowfuse/node-red-dashboard node-red-node-serialport
```

Rodar:

1. `node-red`
2. Abra `http://localhost:1880` → menu → **Import** → cole o conteúdo de
   [`node-red/cervejeira_nodered_SIMULACAO.json`](node-red/cervejeira_nodered_SIMULACAO.json) → **Deploy**.
3. Abra o painel em **`http://localhost:1880/dashboard`**, escolha uma receita e clique
   em **Iniciar / Pausar**.

## 🔌 Ligar no Arduino real — plug-and-play (zero ajuste manual)

**Não precisa mexer em nada no flow.** Ele detecta a porta do Arduino sozinho e alterna
entre simulação e hardware automaticamente:

- **Sem Arduino:** roda em simulação.
- **Conectou o Arduino (USB):** em ~1 s a porta é detectada, o painel passa a mostrar os
  **dados reais** e os botões `P`/`B` controlam o hardware. O simulador **recua sozinho**.
- **Desconectou:** a simulação volta em ~5 s.

Requisito único: o pacote `node-red-node-serialport` (no passo de instalação acima). O
Node-RED instala o `serialport` automaticamente na primeira execução.

> Uma linha `serial port COM255 ... File not found` no log ao iniciar **é normal**: é a
> porta-placeholder enquanto nenhum Arduino está conectado — ela é fechada sozinha.

### Conferir (10 s)

1. Conecte o Arduino por USB.
2. A **temperatura no painel muda sozinha** e o nó **`Detectar porta Arduino`** mostra
   *"Arduino em COMx"*.
3. Não detectou? Veja se a **IDE do Arduino não está com o Serial Monitor aberto** (ele
   prende a porta) e se o baud do firmware é **9600**.

> O modo **Teste / Padrão** é fiel ao firmware atual (65 °C / 2 min). As receitas Pilsen/APA
> são da simulação — o firmware atual não executa mostura por etapas (evolução futura; o
> painel já está preparado para receber os *setpoints* via serial).

## 📡 Protocolo serial (Arduino ⇄ Node-RED · 9600 baud)

- **Arduino → NR** (1×/s): `{"temp":65.0,"proc":1,"rodando":1,"bomba":0,"aquec":1,"dec":12345}`
- **NR → Arduino**: caractere `P` (inicia/pausa processo) ou `B` (liga/desliga bomba, só com processo parado)

## 🔧 Hardware (resumo do firmware)

| Pino | Função |
|---|---|
| 2 | Botão Bomba |
| 3 | Botão Processo |
| 5 | Relé Aquecedor |
| 6 | Relé Bomba (LOW = ligado) |
| 12 | Sensor DS18B20 |
| I²C (0x27) | LCD 20×4 |
