# Plano da Spec: macOS robusto primeiro, GUI-ready depois

## Resumo

- Reposicionar a spec para tratar **macOS 14+ em Apple Silicon** como a única plataforma com critério de aceite de release nesta fase.
- Sequência obrigatória da roadmap: **1. robustez real no Mac**, **2. distribuição portátil no Mac**, **3. contrato da bridge para GUI**, **4. implementação da GUI**, **5. outras arquiteturas**.
- A GUI continua sendo cliente fino sobre runtime/app layer; ela **não começa** antes de o Mac passar pelos gates de qualidade, fallback, tiling e empacotamento.
- A validação passa a usar um **corpus fixo do repositório**, não amostras ad hoc.

## Mudanças principais na spec e nas interfaces

- Atualizar `docs/SPEC.md` para substituir a roadmap genérica por fases com gates explícitos:
  - Fase 1: hardening do runtime no macOS.
  - Fase 2: bundle portátil para terceiros.
  - Fase 3: contrato Tauri/sidecar.
  - Fase 4: GUI.
  - Fase 5: expansão para Windows/Linux.
- Alinhar `docs/ARCHITECTURE.md` e `docs/FRONTEND.md` com a regra: GUI usa **bridge sidecar/Tauri**, sem lógica de negócio duplicada.
- Formalizar política de backend no Mac:
  - `auto` no macOS = **CoreML primeiro, CPU como fallback obrigatório**.
  - Fallback deve ocorrer em falha de detecção, criação de sessão, incompatibilidade do modelo ou erro de execução.
  - Todo fallback deve expor motivo estruturado para CLI/GUI.
- Definir interfaces estáveis para a bridge:
  - `process --json` vira **stream NDJSON** com eventos `job_started`, `backend_selected`, `progress`, `warning`, `artifact_written`, `completed`, `failed`, `cancelled`.
  - `info`, `doctor`, `benchmark`, `models` e `presets` retornam JSON único e estável.
- Adicionar ao contrato de runtime/app os tipos conceituais:
  - `RuntimeCapabilities`: backends detectados, suporte a CoreML, CPU fallback, VideoToolbox, tiling e batching.
  - `JobEvent`: fase, progresso, backend escolhido, motivo de fallback, timings, artefatos e erros.
  - `ModelCatalogEntry` e `PresetDefinition` para GUI e CLI usarem o mesmo catálogo.
- Manter a API pública de processamento enxuta; nesta fase, as ampliações públicas ficam restritas a **diagnóstico, capabilities e eventos estruturados**, não a knobs novos de baixo nível.
- Fixar a política de qualidade/performance para Mac:
  - Modelos validados e empacotados na fase 1: `int8_512` e `int8_768`.
  - `auto` usa `512` em 8 GB e `768` em 16 GB ou mais.
  - Inputs maiores usam **tiling**; não promover `1024` como caminho padrão no Mac antes de validação real.
- Fixar a política de paralelismo:
  - No CoreML, começar com **1 sessão de inferência em voo por modelo**.
  - Paralelizar decode, pré-processamento, montagem de tiles, pós-processamento e escrita.
  - Só liberar inferência concorrente se benchmarks no corpus provarem ganho sem regressão de estabilidade/memória.
- Fixar a política de vídeo no Mac:
  - Usar **VideoToolbox** quando disponível, com fallback para software.
  - Receita de saída padrão para portabilidade: **H.264 em MP4**.
  - HEVC e opções mais agressivas ficam como presets avançados, não como default.
- Fixar o corpus de validação na spec com os assets já existentes:
  - `assets/corridor.png`
  - primeiros 20 frames de `assets/image_samples/thelikelyandunfortunatedemiseofyourgpu`
  - `greenscreen_1769569137.mp4`
  - `greenscreen_1769569320.mp4`
  - `100745-video-2160.mp4`
  - `mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4`
- Para cada item do corpus, a spec deve declarar modo esperado:
  - smoke/simple path
  - standard inference
  - tiled high-resolution
  - stress/performance run

## Plano de implementação derivado da spec

- Hardening de runtime no macOS:
  - detecção real de capabilities Apple Silicon/CoreML/VideoToolbox
  - seleção e fallback de backend com diagnóstico estruturado
  - correção do caminho de vídeo para aceleração real no Mac
  - scheduler de tiling com merge sem seams e paralelismo controlado
- Preparação para GUI:
  - consolidar saída JSON estável
  - adicionar `models` e `presets`
  - padronizar eventos, estados, cancelamento e artefatos
- Distribuição:
  - bundle CLI portátil com binário, dylibs, modelos validados e smoke tests em máquina limpa
  - codesign/notarização entram como próxima etapa imediata, mas não bloqueiam o fechamento do runtime

## Plano de testes e aceite

- Unit:
  - policy de backend/fallback
  - hardware profile do Mac
  - geometria de tiling, overlap e seam blending
  - emissão de eventos estruturados
- Integration:
  - criação de sessão CoreML com modelo real
  - fallback forçado para CPU
  - seleção VideoToolbox vs software
  - bundle portátil executando fora da árvore de build
- Regression:
  - um teste por bug real de macOS: fallback quebrado, seam em tile, erro de provider, problema de lookup de dylib
- E2E / gates para liberar GUI:
  - corpus completo roda sem crash
  - sem frame drop inesperado
  - sem artefato visível de seam nos casos 4K com tiling
  - `info`, `doctor`, `benchmark`, `models`, `presets` e `process --json` estáveis
  - métricas e frames de referência permanecem dentro das tolerâncias definidas na spec

## Assumptions e defaults

- Plataforma primária desta fase: **macOS 14+ Apple Silicon**.
- Primeiro deliverable externo: **bundle CLI portátil**, não `.app`.
- A bridge **Tauri/sidecar** será definida agora na spec, mas a GUI só começa depois dos gates do Mac.
- O corpus de qualidade é **fixo e versionado no projeto**.
- Outras arquiteturas ficam explicitamente **depois** do fechamento da trilha macOS + contrato da GUI.
