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

## Metodo de evolucao e pesquisa

- Toda otimizacao precisa partir de **medicao do pipeline real**.
- A ordem obrigatoria de trabalho passa a ser:
  - capturar `doctor --json`, `benchmark --json` e `process --json`
  - identificar o gargalo dominante por etapa
  - pesquisar a solucao na stack real usada pelo projeto
  - implementar a menor mudanca possivel
  - reexecutar o corpus e aceitar apenas ganhos mensuraveis
- Fontes prioritarias para cada etapa:
  - ONNX Runtime oficial para threading, profiling, graph optimization,
    CoreML EP, TensorRT RTX EP e I/O binding
  - FFmpeg oficial para decode/encode e VideoToolbox
  - documentacao oficial da Apple para profiling no macOS
- O projeto nao deve adotar otimizações por intuicao, folklore de performance
  ou tecnicas transferidas de outras stacks sem validacao no corpus.

## Estado atual validado

- O runtime ja expoe observabilidade suficiente para investigacao guiada por
  evidencia:
  - `doctor --json` inclui bundle, cache, video e modelos validados
  - `benchmark --json` separa cold start, warmup, throughput e timings por
    etapa
  - `models --json` e `presets --json` carregam plataforma, tier e intencao de
    uso
- O bundle macOS ja empacota binario, dylib, modelos validados, smoke test e
  suporte opcional a assinatura/notarizacao via ambiente.
- O cache de modelos otimizados agora escolhe um root gravavel de forma
  resiliente:
  - respeita `CORRIDORKEY_CACHE_DIR` quando gravavel
  - faz fallback automatico para um cache temporario quando o root configurado
    ou padrao nao pode ser usado
  - `doctor --json` explicita `configured_path`, `selected_path`,
    `fallback_in_use` e os candidatos avaliados
- O caminho tiled ja usa reuso de buffers, cache de weight mask e paralelismo
  controlado nas etapas CPU ao redor da inferencia.
- A suite atual passa com:
  - `cmake --build build/debug --parallel`
  - `ctest --test-dir build/debug --output-on-failure`

## Gargalos verificados que dirigem a proxima rodada

- No benchmark sintetico CPU `int8_512`, `ort_run` continua sendo o custo
  dominante.
- `post_despeckle` ainda aparece como custo material no caminho CPU.
- Warmup e first-frame continuam relevantes e precisam de investigacao propria
  no trilho CoreML.
- Falhas de permissao no cache otimizado nao podem mais bloquear criacao de
  sessao; esse caso deve degradar para outro root gravavel ou desabilitar o
  cache de forma limpa.
- O caminho 4K nativo com `int8_768` + `--tiled` ainda e pesado demais para ser
  considerado fechado; a proxima rodada precisa medir e reduzir esse custo sem
  perder resolucao nem qualidade.

## Checkpoints medidos ja confirmados

- Paralelizacao explicita do `despeckle` no fallback CPU melhorou o benchmark
  sintetico limpo `int8_512`:
  - `post_despeckle`: `2614.331 ms` -> `692.164 ms` total (`-73.5%`)
  - `avg_latency_ms`: `1970.097 ms` -> `1674.577 ms` (`-15.0%`)
  - `cold_latency_ms`: `3915.566 ms` -> `3426.698 ms` (`-12.5%`)
- Cache de modelo otimizado por backend reduziu startup entre a primeira e a
  segunda execucao sintetica CPU `int8_512`:
  - `engine_create`: `473.243 ms` -> `110.579 ms` (`-76.6%`)
  - `avg_latency_ms`: `2537.366 ms` -> `1725.103 ms` (`-32.0%`)
  - `ort_run`: `21606.237 ms` -> `17264.443 ms` total (`-20.1%`)
- Medicao controlada 4K com `process --json` no frame
  `build/runtime_inputs/100745-video-2160-frame0.png`, `int8_768`, `cpu`,
  `--tiled`:
  - `batch-size 2` reduziu `job_total` de `180226.274 ms` para
    `171322.677 ms` (`-4.94%`)
  - `batch-size 4` reduziu `job_total` para `170744.913 ms` (`-5.26%`), mas
    aumentou `batch_prepare_inputs` em `+111.34%` e `batch_extract_outputs` em
    `+48.92%`
  - `tile_infer` caiu de `45` chamadas para `23` com `batch-size 2` e `12` com
    `batch-size 4`
  - O ganho adicional de `4` sobre `2` foi pequeno demais nesta maquina para
    justificar promover esse valor como default sem mais corpus e memoria real
- A deteccao macOS agora faz fallback para a arquitetura compilada quando
  `sysctl` falha no ambiente atual:
  - `info --json` voltou a expor `apple_silicon: true`, `coreml_available:
    true` e os devices `coreml` + `cpu`
  - isso virou cobertura unit em `tests/unit/test_device_detection.cpp`
- O proximo bloqueio real do trilho macOS nao e mais deteccao:
  - a criacao de sessao `CoreML` continua falhando na build vendorizada atual
  - o fallback estruturado agora mostra o motivo: `Error compiling model Failed
    to create a working directory appropriate for URL`
  - trocar `TMPDIR` para um path controlado do workspace nao resolveu
  - antes de voltar a medir throughput no provider acelerado, o proximo corte
    deve ser resolver ou contornar esse limite do `CoreML EP`
- A investigacao local confirmou a causa do bloqueio:
  - o vendor atual e `ONNX Runtime 1.16.3`
  - o header local ainda expoe apenas `OrtSessionOptionsAppendExecutionProvider_CoreML`
  - `ModelCacheDirectory` e os provider options novos de `CoreML` nao existem
    nesse vendor
- Trilha de retomada para o upgrade do vendor:
  - source oficial clonado em `vendor/onnxruntime-src` na tag `v1.24.3`
  - a build oficial precisa de Python `3.10+`; aqui ela esta sendo forcada com
    `PATH="/Volumes/MacMini/mise/installs/python/3.13.12/bin:$PATH"`
  - com `CMake 4.2`, foi necessario adicionar
    `CMAKE_POLICY_VERSION_MINIMUM=3.5`
  - a release `v1.24.3` tambem precisou de um patch local em
    `onnxruntime/core/providers/coreml/model/model.mm` para envolver
    `MLOptimizationHints` em `@available`, mantendo target `macOS 14+`
  - comando de build atual:
    `./build.sh --config Release --build_shared_lib --parallel --skip_tests --skip_submodule_sync --use_coreml --cmake_extra_defines CMAKE_OSX_ARCHITECTURES=arm64 CMAKE_OSX_DEPLOYMENT_TARGET=14.0 CMAKE_POLICY_VERSION_MINIMUM=3.5 onnxruntime_BUILD_UNIT_TESTS=OFF`
  - depois que a `libonnxruntime` nova sair, os proximos passos sao:
    1. trocar `vendor/onnxruntime`
    2. validar `process --json -d coreml -i assets/corridor.png`
    3. confirmar que o fallback por diretorio temporario desapareceu
- Estado atual dessa trilha:
  - o vendor local ja foi trocado para `ONNX Runtime 1.24.3`
  - o runtime agora usa `ModelCacheDirectory` para `CoreML`, mas nao tenta mais
    serializar modelo otimizado nesse backend
  - a serializacao otimizada ficou restrita a `CPU`; isso virou cobertura unit
    em `tests/unit/test_session_cache_policy.cpp`
  - no ambiente sandbox do Codex, `process --json -d coreml` ainda cai para CPU
    com erro de diretorio temporario
  - fora do sandbox, o mesmo probe agora sobe em `CoreML` e conclui com
    `backend_selected = coreml`
  - o primeiro benchmark sintetico fora do sandbox mostrou que `CoreML` ainda
    esta mais lento que `CPU` no `int8_512` desta maquina:
    - `avg_latency_ms`: `2789.437 ms` em `coreml` vs `1500.269 ms` em `cpu`
    - `cold_latency_ms`: `5847.240 ms` em `coreml` vs `3508.227 ms` em `cpu`
    - `session_create_requested`: `1270.750 ms` em `coreml` vs `218.895 ms`
      em `cpu`
    - `ort_run`: `24496.280 ms` total em `coreml` vs `12902.352 ms` em `cpu`
- Proximo corte depois deste ponto:
  - medir `CoreML` em `benchmark --json` e `process --json` fora do sandbox
  - comparar startup real, warmup e `ort_run` com o fallback `CPU`
  - voltar para o workload 4K tiled com o provider acelerado funcional
- Mesmo com esses ganhos, `ort_run` continua sendo o gargalo principal.
- Ao medir o workload 4K tiled no ambiente sandbox atual, apareceu um bug real:
  o cache otimizado tentava gravar em um root nao permitido e falhava em
  `session_create`. Esse caso agora virou regressao coberta em teste de
  integracao.

## Como retomar sem perder contexto

- Primeiro, executar o baseline operacional:
  - `./build/release/src/cli/corridorkey doctor --json`
  - `./build/release/src/cli/corridorkey benchmark --json -m models/corridorkey_int8_512.onnx -d cpu`
- Para uma retomada reproduzivel, usar `scripts/run_corpus.sh`:
  - `CORRIDORKEY_CORPUS_PROFILE=smoke` para validacao curta
  - `CORRIDORKEY_CORPUS_PROFILE=baseline` para o baseline de trabalho
  - `CORRIDORKEY_CORPUS_PROFILE=full` para a rodada completa do corpus
- Para comparar uma rodada com outra, usar:
  - `python3 scripts/compare_benchmarks.py before.json after.json`
- Depois, gerar artefatos do corpus em pasta fora dos arquivos rastreados:
  - salvar `doctor`, `benchmark` e `process --json`
  - registrar qual asset foi usado, modelo, backend, preset e se houve fallback
- So entao escolher o proximo corte tecnico.
- Ordem da proxima execucao:
  - startup/CoreML e first-frame
  - `ort_run` e configuracao de sessao/modelo
  - `post_despeckle` no fallback CPU
  - throughput do caminho 4K tiled

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
