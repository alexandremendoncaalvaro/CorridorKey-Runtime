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
- O runtime e unificado, mas o **artefato de modelo passa a ser curado por
  plataforma**. O produto nao assume mais que um mesmo ONNX empacotado sera o
  caminho certo para Apple Silicon e para Windows RTX.
- Python continua permitido apenas nas ferramentas internas de conversao e
  preparo de release. O artefato distribuido para usuarios finais nao pode
  depender de Python para instalar ou executar.

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
  - contratos de runtime unificados
  - `int8_512` e `int8_768` como baseline de compatibilidade e diagnostico
  - CPU como fallback obrigatorio
  - aceleracao Mac passa a exigir **artefato especifico de plataforma**
  - trilhos aprovados para avaliacao: `MLX` e conversao direta para `Core ML`
  - `ONNX int8 -> ORT CoreML` deixa de ser plano principal e vira baseline de
    diagnostico/compatibilidade
  - bundle CLI portatil como primeiro artefato externo
- **Depois: Windows 11 + NVIDIA RTX**
  - TensorRT RTX como caminho principal de produto
  - artefato ONNX/RTX proprio para a plataforma
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

- Estado atual do macOS:
  - bundle portatil validado fora da arvore de build
  - fluxo padrao do usuario no bundle reduzido a `doctor`, `process input output`
    e `process input output --preset max`
  - pack Apple empacotado no release, sem dependencia de Python para o usuario
  - validacao final ainda separa dois niveis:
    - funcional: concluido para o bundle local
    - Gatekeeper publico: depende de assinatura/notarizacao com credenciais da
      conta Apple de release
- Proxima transicao de produto depois do bundle Mac:
  - fechar assinatura/notarizacao do artefato oficial
  - iniciar baseline de Windows e, em seguida, trilho Windows RTX

- Fechar a decisao do trilho acelerado para macOS:
  - tratar `ONNX int8 -> ORT CoreML` atual apenas como baseline investigativo
  - avaliar `MLX` contra conversao direta `PyTorch -> Core ML`
  - comparar instalacao, empacotamento, integracao no runtime, qualidade,
    throughput e previsibilidade operacional
  - escolher um caminho principal antes de voltar a otimizar throughput em
    baixo nivel
- Fechar os gates de macOS no caminho escolhido:
  - fallback acelerado -> CPU robusto e explicado estruturalmente
  - warmup/startup, tiling e video medidos por etapa
  - bundle portatil rodando fora da arvore de build
  - corpus completo sem crash e sem seams visiveis em 4K tiled
- Preparar o trilho Windows RTX na documentacao e na arquitetura:
  - provider principal definido
  - recorte de hardware alvo definido
  - comportamento operacional esperado definido
- Definir a superficie de conversao/empacotamento de modelos:
  - checkpoint fonte canonico
  - empacotamento por target (`mac-mlx`, `mac-coreml`, `win-rtx`, `cpu`)
  - validacao por corpus e catalogo versionado no runtime
- Preservar contratos e observabilidade como superficie publica:
  - JSON unico para `info`, `doctor`, `benchmark`, `models`, `presets`
  - NDJSON estavel em `process --json`
  - timings agregados suficientes para localizar gargalos

## Comparacao objetiva dos trilhos acelerados do Mac

### Evidencia externa que sustenta a comparacao

- O repositorio original do CorridorKey ja separa o Mac do restante:
  - `Torch` em Linux/Windows
  - `MLX` em Apple Silicon
  - auto mode preferindo `MLX` no Apple Silicon quando disponivel
- O repositorio `corridorkey-mlx` ja tem:
  - conversao de checkpoint `PyTorch -> MLX`
  - pesos `.safetensors`
  - e2e parity
  - fase de benchmark marcada como concluida
- A documentacao oficial do `MLX` confirma:
  - API C++ completa
  - API C
  - memoria unificada
  - exportacao/importacao de funcoes entre Python e C++
- A Apple recomenda converter de `PyTorch` para `Core ML` **diretamente**,
  sem usar ONNX como etapa obrigatoria.
- A Apple tambem documenta que a quantizacao padrao do PyTorch nao e a melhor
  para hardware Apple; o caminho recomendado passa por
  `coremltools.optimize.torch`.

### Matriz de decisao atual

- **MLX**
  - melhor candidato para chegar mais rapido a um Mac acelerado funcional
  - mais alinhado com o caminho ja validado pelo projeto original
  - favorecido por memoria unificada e stack nativa Apple Silicon
  - nao exige insistir no grafo `ONNX int8` atual
  - risco principal: a implementacao CorridorKey disponivel hoje e Python-first,
    entao a integracao no nosso runtime C++ vai exigir um adapter nativo, uma
    trilha via export/import de funcoes do MLX, ou uma reimplementacao pontual
- **PyTorch -> Core ML direto**
  - melhor candidato para o artefato final de distribuicao nativa no Mac
  - mais alinhado com a recomendacao oficial da Apple para conversao
  - conversa melhor com empacotamento `.mlpackage` / compilacao e uso em app
  - evita a limitacao atual de cobertura parcial do `CoreML EP` via ONNX
  - risco principal: a conversao precisa ser validada no checkpoint real do
    CorridorKey e o caminho de conversao ainda pode exigir ajustes de grafo,
    tracing/export e tratamento de ops
- **ONNX int8 -> ORT CoreML atual**
  - permanece util como baseline de compatibilidade e diagnostico
  - nao deve mais ser tratado como trilho principal de aceleracao Mac
  - a evidencia atual mostra cobertura parcial de grafo e performance pior que
    CPU em workload relevante

### Recomendacao operacional provisoria

- Se a meta imediata for **provar aceleracao real no Mac**:
  - priorizar `MLX`
- Se a meta imediata for **fechar o artefato final de produto distribuivel no
  Mac**:
  - avaliar em paralelo a conversao direta `PyTorch -> Core ML`
- O caminho recomendado por ora e:
  1. usar `MLX` para validar rapidamente o trilho acelerado Apple Silicon
  2. fazer um spike controlado de conversao `PyTorch -> Core ML`
  3. escolher o caminho de shipping do Mac com base no corpus e no bundle

### Conclusao validada da rodada MLX

- O release oficial de `corridorkey-mlx` publica pesos em
  `corridorkey_mlx.safetensors`; esse e o artefato real de curto prazo para o
  trilho Mac, nao `.onnx` e nem `.mlxfn`.
- O downloader instalado localmente ainda aponta para um owner default
  incorreto, mas o release oficial existe e foi baixado com override explicito
  de repositorio.
- Benchmark sintetico com pesos oficiais:
  - `512`: `compiled` melhor que `eager`
    - `eager`: `401.980 ms` first, `406.333 ms` steady
    - `compiled`: `373.016 ms` first, `351.343 ms` steady
  - `1024`: `compiled` pior que `eager` neste ambiente
    - `eager`: `1887.215 ms` first, `1886.611 ms` steady
    - `compiled`: `1956.788 ms` first, `3129.624 ms` steady
  - conclusao: `mx.compile` nao pode ser promovido como default cego; precisa
    de tuning por resolucao e validacao no corpus
- Workload real 4K no frame
  `build/runtime_inputs/mixkit-4k-frame0.png` com hint coarse controlado:
  - `MLX tiled_1024`: `load_ms = 3630.719`, `run_ms = 52354.714`,
    `peak_memory_mb = 3487.092`
  - runtime atual `CPU` com `corridorkey_int8_768.onnx`, `--tiled` e
    `--batch-size 2`, no mesmo frame e no mesmo hint:
    `job_total = 110488.172 ms`
  - ganho observado: `MLX` ficou `2.11x` mais rapido que o baseline CPU desta
    maquina no caso real controlado
- O path `ONNX -> ORT CoreML EP` continua fora da disputa principal:
  - segue pior que CPU nos workloads medidos
  - o provider ainda particiona o grafo atual para CPU
- A ponte nativa futura tambem foi validada:
  - exportacao `MLX -> .mlxfn` funcionou em `512`
  - arquivo gerado: `build/mlx_eval/corridorkey_512.mlxfn`
  - tamanho: `288931878` bytes
  - importacao e execucao do wrapper funcionaram
  - isso prova que existe ponte real para integracao nativa futura, mas nao
    muda o artefato primario imediato do Mac

### Decisao de reestruturacao que segue desta rodada

- O primeiro backend acelerado real do Mac deve ser **MLX com
  `.safetensors`**, tratado como trilho de produto proprio.
- O pack Apple de shipping passa a ser tratado como **`corridorkey_mlx.safetensors`
  + bridge exports `.mlxfn` curados por plataforma**.
- O trilho `PyTorch -> Core ML` direto continua necessario, mas muda de papel:
  - deixa de ser a primeira tentativa de aceleracao do runtime
  - passa a ser o candidato principal para o **artefato final distribuivel**
    no bundle do Mac
- O caminho `ONNX/CoreML EP` fica restrito a:
  - diagnostico
  - comparacao
  - fallback investigativo
  - nunca como trilho principal da reestruturacao do Mac

### Estado operacional validado do trilho MLX

- O repositório agora tem um helper reproduzível para preparar o pack Apple:
  - `scripts/prepare_mlx_model_pack.py`
  - cobre download do release oficial, conversao de `.pth -> .safetensors` e
    exportacao dos bridge exports exigidos pelo bundle (`512` e `1024`)
- A descoberta do SDK do MLX no CMake foi endurecida para priorizar:
  - `CORRIDORKEY_MLX_CMAKE_DIR`
  - `CORRIDORKEY_MLX_PYTHON`
  - `VIRTUAL_ENV`
  - `.venv-macos-mlx`
  - `Python3_EXECUTABLE`
- Com o model pack preparado em `models/` e o build reconfigurado, o estado
  esperado no `doctor --json` ficou validado:
  - `mlx.probe_available = true`
  - `mlx.primary_pack_ready = true`
  - `mlx.bridge_ready = true` quando os bridge exports empacotados estao
    presentes e importaveis
  - `mlx.backend_integrated = true` quando o runtime esta linkado com MLX e o
    pack Apple empacotado executa corretamente
  - `summary.apple_acceleration_probe_ready = true`
- A execucao do backend MLX dentro do runtime agora ficou validada no pack
  Apple de shipping:
  - `corridorkey process --json -d mlx -i assets/corridor.png -o ... -m models/corridorkey_mlx.safetensors`
    completou com sucesso
  - `doctor --json` agora marca `integration_mode = mlx_pack_with_bridge_exports`
  - os bridge exports continuam sendo um detalhe interno do pack Apple, nao o
    artefato publico principal do Mac
- O contrato operacional do Mac foi simplificado para nao carregar defaults
  legados:
  - `auto` agora e **model-aware** no runtime
  - `corridorkey_mlx.safetensors` e `.mlxfn` resolvem para `MLX`
  - `.onnx` no Mac resolve para `CPU`, nao para `CoreML`
  - `mac-balanced` aponta para `corridorkey_mlx.safetensors`
  - `doctor.summary.healthy` depende de `apple_acceleration_healthy`, nao de
    `coreml_healthy`
  - `int8_768` deixou de ser artefato empacotado padrao do bundle Mac
- O bug real da rodada foi identificado e corrigido:
  - o `JobOrchestrator` estava resolvendo `target_resolution` antes de criar o
    engine
  - isso quebrava bridges fixos como `corridorkey_mlx_bridge_512.mlxfn`
  - a resolucao automatica agora respeita `engine->recommended_resolution()`
    quando o backend selecionado e `MLX`
- O runtime agora escolhe o bridge export pelo tier real de memoria:
  - `>= 16 GB`: `1024`, depois `768`, depois `512`
  - `>= 12 GB`: `768`, depois `512`, depois `1024`
  - abaixo disso: `512`, depois `768`, depois `1024`
- Benchmark atual validado do runtime:
  - `MLX` synthetic `512`: `avg_latency_ms = 601.189`
  - `CPU` synthetic `512` (`corridorkey_int8_512.onnx`): `avg_latency_ms = 1050.283`
  - ganho observado do trilho MLX atual: aproximadamente `1.75x`
- Validacao externa do bundle Mac agora passa fora da arvore de build:
  - `scripts/validate_mac_release.sh` recria o zip, extrai o bundle, roda o
    smoke test, valida `doctor`, gera benchmark sintetico, benchmark 4K por
    frame, `process --json` 4K e confirma preservacao de resolucao
  - ultimo resumo validado:
    - `bundle_healthy = true`
    - `doctor_healthy = true`
    - `frame_4k_backend = mlx`
    - `output_matches_input_resolution = true`

### Plano tecnico para fechar o hot path de video MLX

- O estado atual ja prova que o runtime:
  - abre videos reais da pasta `assets/video_samples`
  - seleciona `MLX`
  - entra no pipeline de inferencia
  - e agora ja completa um sample `2048x2048` end-to-end com o backend `MLX`
- A leitura embasada desta rodada ficou assim:
  - o gargalo inicial do path MLX estava mesmo no jeito como o bridge era
    exportado e executado, nao no MLX em si
  - corrigir o `prepare_mlx_model_pack.py` para usar `compile_model(...)` de
    fato, somado a `mlx::core::compile(...)` no runtime, derrubou o custo
    dominante do bridge
  - no frame `4K tiled`, o path com `corridorkey_mlx.safetensors` caiu de
    `38972.608 ms` para `26111.325 ms` (`-33.0%`)
  - a telemetria agora separa `mlx_materialize_outputs` de
    `mlx_copy_outputs`, e mostrou que a copia host ficou pequena
  - o bridge `512` venceu `1024` no corpus atual com folga:
    - `4K tiled`: `17035.608 ms` vs `29708.233 ms`
    - `assets/corridor.png`: `871.149 ms` vs `4130.743 ms`
  - portanto o pack Apple passa a preferir `512` por padrao
- Resultado validado no sample de video:
  - `assets/video_samples/greenscreen_1769569137.mp4`
  - `corridorkey_mlx_bridge_512.mlxfn`
  - `40` frames processados
  - `job_total = 55623.560 ms`
  - resolucao de saida preservada em `2048x2048`
- O gargalo dominante mudou de lugar:
  - `post_despeckle = 40190.533 ms`
  - `video_infer_batch = 54767.260 ms`
  - `mlx_materialize_outputs = 12826.733 ms`
- Isso conversa com o `corridorkey-mlx`, que hoje nao aplica
  `despeckle/despill` no backend MLX. O caminho padrao do Mac deixa de ativar
  cleanup implicitamente; `--despeckle` fica reservado para runs mais lentos e
  orientados a qualidade.
- Proxima rodada do Mac:
  1. rerodar bundle + corpus baseline com o default `512`
  2. validar `process --json` end-to-end no sample `4K`
  3. decidir se `despeckle` precisa de otimizacao propria ou se fica apenas no
     preset/caminho de max quality

### Criterios de escolha entre MLX e Core ML direto

- roda o corpus de release sem crash e sem seams
- preserva qualidade visual e tamanho de saida
- integra no runtime/library-first sem Python no artefato distribuido
- cabe num bundle instalavel por terceiros com friccao controlada
- permite diagnostico e fallback claros em `doctor`, `benchmark` e
  `process --json`
- entrega throughput melhor que o baseline CPU no hardware alvo

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
- O primeiro workload real `4K tiled` no frame
  `build/runtime_inputs/100745-video-2160-frame0.png` confirmou que `CoreML`
  ainda nao esta entregando ganho real sobre `CPU`:
  - `coreml`: `job_total = 160143.254 ms`
  - `cpu --batch-size 2`: `job_total = 150022.264 ms`
  - `coreml` tambem ficou pior em startup e warmup:
    - `engine_create`: `1480.884 ms` vs `369.753 ms`
    - `engine_warmup`: `6823.230 ms` vs `3652.920 ms`
  - o `sample` do processo `coreml` mostrou tempo dominante dentro de kernels
    `MLAS` de `CPU` no `onnxruntime`, nao em stack real de aceleracao `CoreML`
- A causa mais provavel agora esta validada por diagnostico do proprio runtime:
  - o `CoreML EP` esta assumindo apenas parte do grafo e deixando o restante no
    `CPU`
  - o `doctor --json` agora faz um probe por modelo com
    `session.disable_cpu_ep_fallback=1`
  - resultado atual do probe:
    - `corridorkey_int8_512.onnx`: falha
    - `corridorkey_int8_768.onnx`: falha
    - erro: `This session contains graph nodes that are assigned to the default CPU EP, but fallback to CPU EP has been explicitly disabled by the user.`
- Proximo corte depois deste ponto:
  - formalizar a decisao de produto: **artefatos por plataforma sao
    obrigatorios**
  - usar o caminho `ONNX/CoreML` atual apenas para diagnostico do problema,
    nao como meta principal de aceleracao Mac
  - comparar `MLX` e conversao direta `PyTorch -> Core ML` com base em fonte
    oficial e no repositorio original do CorridorKey
  - executar primeiro um spike `MLX`, porque ele e o candidato mais forte para
    provar aceleracao real no Apple Silicon
  - em seguida executar um spike de conversao direta `PyTorch -> Core ML` para
    decidir o artefato final de shipping do Mac
  - so depois escolher o trilho acelerado principal do Mac e retomar tuning de
    throughput
- Mesmo com esses ganhos, `ort_run` continua sendo o gargalo principal.
- Ao medir o workload 4K tiled no ambiente sandbox atual, apareceu um bug real:
  o cache otimizado tentava gravar em um root nao permitido e falhava em
  `session_create`. Esse caso agora virou regressao coberta em teste de
  integracao.

## Como retomar sem perder contexto

- Primeiro, lembrar a decisao de arquitetura:
  - runtime e contratos unificados
  - artefatos e backends acelerados sao curados por plataforma
  - `ONNX int8 -> ORT CoreML` atual e baseline de diagnostico, nao plano
    principal de aceleracao Mac
- Primeiro, executar o baseline operacional:
  - `./build/release/src/cli/corridorkey doctor --json`
  - `./build/release/src/cli/corridorkey benchmark --json -m models/corridorkey_int8_512.onnx -d cpu`
- Antes de qualquer nova otimizacao no Mac, responder estas perguntas:
  - `MLX` consegue entregar uma integracao library-first viavel para o produto?
  - a conversao direta `PyTorch -> Core ML` produz um artefato melhor para
    distribuicao do que o `ONNX int8` atual?
  - qual trilho deixa instalacao, bundle e suporte mais previsiveis?
  - qual dos dois bate o baseline CPU no corpus real do Mac?
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
  - probe de compatibilidade `CoreML` por modelo
  - `Model Usability Checker` e particionamento do grafo
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
