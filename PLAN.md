# Direcao de produto: engine nativa, distribuivel e integravel

## Resumo

- O produto deve ser comunicado como **engine nativa de producao** para
  executar CorridorKey com previsibilidade operacional em hardware real.
- A sequencia de entrega deixa de parecer aberta ou distante:
  **1. macOS first**, **2. Windows RTX next**, **3. superficies de integracao**,
  **4. expansao ampla de plataforma**.
- O foco imediato continua sendo fechar o runtime de producao no macOS, mas a
  documentacao precisa mostrar que Windows RTX ja e o proximo trilho explicito
  do produto.
- O valor central nao e "suportar muitos backends"; e entregar **instalacao
  simples, diagnostico confiavel, benchmark reproduzivel, contratos estaveis e
  comportamento consistente por tier de hardware**.

## Posicionamento do produto

- O projeto nao deve soar como "port cross-platform" do CorridorKey.
- A mensagem principal passa a ser:
  **CorridorKey como engine nativa, distribuivel e integravel, pensada para
  hardware real e uso reproduzivel em produto.**
- `library-first` permanece pilar central:
  CLI, GUI futura, sidecar, plugin e integracao em pipeline usam o mesmo core.
- `doctor`, `benchmark`, `models`, `presets`, `process --json` e telemetria por
  etapa deixam de ser acessorios e passam a ser parte da promessa do produto.

## Trilhos de plataforma

- **Agora: macOS 14+ Apple Silicon**
  - CoreML como caminho principal
  - CPU como fallback obrigatorio
  - `int8_512` e `int8_768` como modelos validados e empacotados
  - bundle CLI portatil como primeiro artefato externo
- **Depois: Windows 11 + NVIDIA RTX**
  - TensorRT RTX como caminho principal de produto
  - CPU como fallback obrigatorio
  - DirectML e WinML tratados como secundarios ou exploratorios
  - foco em instalacao previsivel, diagnostico, cache e benchmark reproduzivel
- **Mais tarde**
  - GUI e sidecar como consumidores finos do runtime
  - Linux e outros caminhos so depois de macOS + Windows RTX estarem claros e
    validados

## Sequencia de entrega

### Fase 1 — macOS production runtime

- robustez real de backend e fallback
- performance observavel guiada por benchmark
- tiling confiavel para high-resolution
- bundle CLI portatil para terceiros
- corpus validado e exemplos de qualidade

### Fase 2 — Windows RTX track

- contrato de provider e instalacao para RTX consumer
- estrategia de cache, compilacao e diagnostico para TensorRT RTX
- tiers e modelos validados para hardware RTX
- `doctor` e `benchmark` equivalentes ao trilho macOS

### Fase 3 — integration surfaces

- contrato sidecar/Tauri estabilizado sobre JSON/NDJSON ja existentes
- GUI como cliente fino do runtime
- abertura explicita para plugin e pipeline embedding

### Fase 4 — broader platform expansion

- Linux e demais caminhos depois de macOS e Windows RTX
- validacao adicional apenas quando houver proposta de valor clara

## Backlog imediato

- Fechar os gates de macOS:
  - fallback CoreML -> CPU robusto e explicado estruturalmente
  - warmup/startup, tiling e video medidos por etapa
  - bundle portatil rodando fora da arvore de build
  - corpus completo sem crash e sem seams visiveis em 4K tiled
- Preparar o trilho Windows RTX na documentacao e na arquitetura:
  - provider principal definido
  - recorte de hardware alvo definido
  - comportamento operacional esperado definido
- Preservar contratos e observabilidade como superficie publica:
  - JSON unico para `info`, `doctor`, `benchmark`, `models`, `presets`
  - NDJSON estavel em `process --json`
  - timings agregados suficientes para localizar gargalos

## AlphaHint como parte do valor percebido

- O runtime aceita hints externos como contrato estavel:
  frame, diretorio de frames ou video.
- Rough matte interno continua existindo como fallback de conveniencia.
- A documentacao deve deixar claro:
  - quando hint externo e preferivel
  - o que acontece quando hint esta ausente
  - o impacto esperado em qualidade e throughput
- Esta fase nao inclui prometer um sistema first-party completo de geracao de
  hints.

## Criterios de aceite desta direcao

- A abertura dos docs nao pode mais fazer o projeto parecer um framework
  multi-backend generico.
- O leitor precisa entender rapidamente:
  - o que e o produto
  - para quem ele existe
  - por que nao e apenas uma reimplementacao tecnica
  - o que esta sendo entregue agora
  - o que vem logo em seguida
- macOS deve aparecer como foco atual.
- Windows RTX deve aparecer como proximo foco ja definido.
- GUI deve aparecer como superficie posterior sobre contratos estaveis.
- AlphaHint, contratos de entrada/saida e metricas observaveis precisam ficar
  explicitos nos docs principais.

## Defaults adotados

- Plataforma com release gate atual: **macOS 14+ Apple Silicon**.
- Proximo trilho explicito de produto: **Windows 11 + NVIDIA RTX**.
- Provider principal de Windows next: **TensorRT RTX**.
- DirectML e WinML nao sao promovidos como eixo principal nesta fase.
- AlphaHint e tratado como **contrato externo + fallback interno**, nao como
  produto de autoria nesta etapa.
