# Manifesto de Arquitetura — CorridorKey Runtime

Este documento define a filosofia, os princípios e a estratégia de evolução do projeto. Ele serve como a **Constituição Técnica** para garantir que o runtime cresça de forma modular, sustentável e desacoplada.

## Objetivo do Projeto

O foco deste projeto é ser um **runtime utilizável de verdade**, com ênfase em:

* Execução local
* Portabilidade
* Baixo atrito de uso
* Possibilidade de rodar em hardware comum
* Arquitetura preparada para múltiplas interfaces sem acoplamento indevido

A prioridade **não** é construir uma interface bonita primeiro.
A prioridade é construir uma base sólida, previsível e reutilizável, para que diferentes interfaces possam ser adicionadas depois sem retrabalho.

---

## Princípios de Arquitetura

Siga estes princípios obrigatoriamente:

1. **Core e interface não podem se misturar**
   * Lógica de inferência, vídeo, modelos, hinting, seleção de backend e pipeline principal devem ficar fora de qualquer interface.
   * TUI, CLI, Web UI ou Desktop UI devem ser apenas **clientes** da camada de aplicação.

2. **A CLI deve ser o primeiro contrato estável do sistema**
   * Antes de criar interfaces mais elaboradas, padronize a CLI.
   * Ela deve ser tratada como uma API de execução local.
   * Tudo que uma TUI ou frontend fizer deve poder ser representado primeiro na CLI.

3. **Toda interface deve ter um propósito claro**
   * **CLI**: Automação, scripting, uso avançado, integração.
   * **TUI**: Melhor UX no terminal, operação guiada, presets, acompanhamento de progresso.
   * **API/daemon local**: Etapa futura, para permitir frontends mais ricos e integrações desacopladas.
   * **Web/Desktop UI**: Etapa posterior, construída sobre contratos já estáveis.

4. **Evolução progressiva**
   * Implementar em camadas.
   * Evitar criar infraestrutura prematura.
   * Evitar frontends complexos antes de estabilizar runtime, jobs, presets e contrato de saída.

5. **Modularidade real**
   * Componentes devem ser reutilizáveis e independentes.
   * Tudo que for possível reaproveitar entre CLI/TUI/API deve ser isolado em módulos próprios.

---

## Estrutura Arquitetural Desejada

O sistema é organizado em camadas hierárquicas estritas:

### 1. Core (`src/core`, `src/frame_io`, `src/post_process`)

Responsável apenas pelo motor e pelas capacidades fundamentais do runtime.

Deve conter, no mínimo:
* Inferência (ONNX Runtime)
* Carregamento e gestão de modelos
* Seleção e abstração de backend (Hardware Tiers)
* Pipeline de vídeo (FFmpeg)
* Auto-hinting / hint generation
* Color pipeline (ColorUtils)
* Escrita de outputs (EXR/PNG/Video)

O core:
* **Não deve conhecer TUI**
* **Não deve conhecer Web UI**
* **Não deve conter parsing de argumentos de interface**
* **Não deve conter regras de apresentação**

---

### 2. Camada de Aplicação (`src/app`)

Responsável por orquestrar o uso do core em fluxos coerentes de execução.

Deve conter, no mínimo:
* Definição de jobs
* Presets
* Validação de entrada
* Escolha de estratégia de execução (Tiling vs Standard)
* Seleção de backend com fallback
* Profile por hardware
* Progresso
* Relatórios e metadados de execução (JSON output)
* Erros padronizados

Essa camada é a ponte entre o motor e qualquer interface.

---

### 3. Contrato da CLI (`src/cli`)

A CLI deve ser tratada como o **primeiro cliente oficial** da camada de aplicação e como o **contrato externo inicial do sistema**.

Ela deve:
* Ser previsível
* Ter comandos consistentes
* Ter nomes claros
* Ter saída humana e saída estruturada (`--json`)
* Ter códigos de saída confiáveis
* Servir de base para futuras integrações

Comandos sugeridos:
* `process`
* `info`
* `doctor` (diagnóstico de ambiente)
* `benchmark`
* `models` (gestão de modelos)
* `presets`

Requisitos importantes:
* Suporte a `--json`
* `stdout` reservado para saída estruturada quando aplicável
* `stderr` para logs humanos e progresso
* Exit codes consistentes
* Mensagens de erro padronizadas
* Compatibilidade com automação e parsing externo

---

### 4. Interfaces Clientes

As interfaces devem ser construídas como clientes da camada de aplicação ou da API/CLI, sem reimplementar regras de negócio.

#### TUI (Terminal User Interface)
A TUI é a próxima camada desejada após estabilizar a CLI.

Objetivo:
* Deixar o runtime mais utilizável sem perder leveza.
* Oferecer operação guiada no terminal.
* Facilitar escolha de input/output/modelo/preset/backend.
* Exibir progresso, status, métricas e mensagens de erro de forma mais amigável.

A TUI:
* Não deve conter lógica central de inferência.
* Não deve virar o centro arquitetural do projeto.
* Deve consumir contratos estáveis da aplicação/CLI.

#### API/Daemon Local (Futuro)
Planejar, mas só implementar quando fizer sentido.

Objetivo:
* Expor o runtime para frontends mais ricos.
* Permitir fila de jobs, status, observabilidade e integração desacoplada.

#### Web/Desktop UI (Futuro)
Devem ser considerados clientes futuros.
Não devem ser a prioridade inicial.

---

## Direção de Implementação

Siga esta ordem de implementação:

### Fase 1 — Consolidar o Runtime e a Camada de Aplicação
Entregar:
* Core organizado.
* Separação clara de responsabilidades.
* Abstrações reutilizáveis.
* Presets e profiles.
* Seleção de backend.
* Progresso e erros padronizados.

### Fase 2 — Estabilizar a CLI
Entregar:
* Comandos bem definidos.
* Saída estruturada.
* Contrato estável.
* Automação confiável.
* Experiência boa para usuário técnico.

### Fase 3 — Implementar TUI
Entregar:
* Interface guiada no terminal.
* Foco em UX sem violar modularidade.
* Consumo do contrato já consolidado.
* Operação mais acessível para uso real.

### Fase 4 — Preparar API/Daemon Local
Somente se houver base suficiente e necessidade real.

### Fase 5 — Explorar Frontends Gráficos
Somente depois de a base estar madura.

---

## Qualidade Esperada

Ao implementar, garanta:
* Baixo acoplamento
* Alta coesão
* Separação clara entre domínio, orquestração e interface
* Código preparado para evolução incremental
* Contratos explícitos
* Nomenclatura consistente
* Extensibilidade sem reescrever a base

Evite:
* Lógica de negócio dentro da TUI
* Decisões de arquitetura guiadas apenas por conveniência momentânea
* Criar web/desktop cedo demais
* Duplicação de regras entre interfaces
* Acoplamento entre parsing de CLI e lógica de aplicação
* Misturar concerns de apresentação com concerns de runtime
