# TorchTRT Pipeline Readiness Plan

## Contexto

CorridorKey Runtime esta preparando o pre-release Windows RTX dinamico. O
objetivo imediato e reduzir risco antes de teste manual no Resolve e no Nuke.
O candidato principal para Green e o artefato dinamico true TorchTRT. O Blue
dinamico atual permanece valido em 2048, mas usa TorchScript/LibTorch; Blue
true TorchTRT FP16 foi rejeitado por NaN em 2048, e Blue true TorchTRT FP32
nao fechou 2048 na RTX 3080 10 GB.

O problema observado em Resolve e desempenho E2E: a inferencia bruta do modelo
nao explica sozinha a latencia do Green. A investigacao deve medir a pipeline
inteira: host OFX, shared memory, preparo de entrada, inferencia, resize,
post-process, copia de saida, cache e escrita OFX. Parametros opcionais tambem
fazem parte do contrato de desempenho porque usuarios podem ativar Source
Passthrough, Despeckle, Lanczos, matte-only, processed output, tiling e modos
de cor compartilhados.

## Evidencia Atual

- `main` e ancestral direto desta branch; nao ha commits de `main` pendentes
  para portar.
- A branch ja contem otimizacoes que a `main` nao tinha no caminho TorchTRT:
  preparo CUDA com staging pinned, evento de prontidao, tensor planar em GPU,
  source passthrough/despill em CUDA, output direto para shared memory pelo
  broker, telemetry GPU para replay/direct, e harness RPC com casos nao
  constantes.
- A `main` mantem o post-process geral em CPU. Portanto, lacunas de
  post-process GPU nesta branch nao sao "port perdido da main"; sao trabalho
  restante para tornar o caminho TorchTRT definitivo.
- A pipeline OFX ainda materializa alpha/foreground no cliente antes de cache,
  ajustes por instancia e escrita OFX. Esse custo e real e deve ser medido,
  mas nao existe implementacao melhor pronta na `main`.
- Os logs atuais do Resolve com o pacote instrumentado mostram
  `gpu_prepare_device` em cerca de 7 a 13 ms e
  `gpu_prepare_wait_over_device` em cerca de 834 a 1445 ms. O replay do modelo
  permanece em cerca de 276 a 289 ms na maioria dos frames, com queue wait
  quase zero.
- O mesmo build no harness nao reproduz esse wait: `gpu_prepare_wait_over_device`
  fica em 0 ms e Green 2048 processado fica perto de 508 a 518 ms. Logo, o
  bloqueio atual e especifico da fronteira Resolve -> GPU prep stream ->
  TorchTRT stream.
- Depois do fix do default stream, os logs do Resolve confirmam que o fallback
  CPU saiu e que `torchtrt_cuda_graph_input_copy_queue_wait` caiu para cerca de
  0.15 ms. O gargalo restante e `torchtrt_input_ready_wait` em cerca de 0.84 a
  1.38 s, apesar de `gpu_prepare_device` medir cerca de 7 a 13 ms.
- Depois do fix de readiness sync, os logs do Resolve no pacote
  `0.8.5-win.1-59-g7f5514a` confirmam `torchtrt_input_ready_wait` e
  `gpu_prepare_wait_over_device` em zero. O gargalo restante foi movido para
  `torchtrt_cuda_graph_input_copy_queue_wait`, cerca de 840 a 1249 ms, enquanto
  `torchtrt_cuda_graph_input_copy_gpu` permanece perto de 0.10 ms.
- O ADR-0004 foi implementado: a sessao TorchTRT tem work stream propria do
  pool do PyTorch e instala `CUDAStreamGuard` no escopo do prepared-input path.
  O harness limpo confirma queue waits pequenos nessa configuracao.
- A validacao filtrada no Resolve do pacote `0.8.5-win.1-61-g6c6df24`, PID
  unico `40112`, confirma `gpu_prepare_wait_over_device=0` e
  `torchtrt_input_ready_wait=0`, mas ainda mostra
  `torchtrt_input_copy_queue_wait` em media cerca de 806 ms e maximo cerca de
  1249 ms. O gargalo P0 agora e Task-0005: diferenciar interacao CUDA Graph
  static-input-copy de contencao host/contexto do Resolve.
- A `main` evita essa fronteira sincronizando o preparo GPU e retornando para
  host antes da inferencia. Isso e uma evidencia historica de estabilidade, mas
  nao e a solucao principal porque perderia a otimizacao device-input desta
  branch.
- A validacao Resolve do pacote `0.8.5-win.1-72-g4b72798` ainda subiu com
  `cuda_graph_env=1` por default e mostrou
  `torchtrt_cuda_graph_input_copy_queue_wait` em media cerca de 1055 ms,
  enquanto o replay GPU ficou em cerca de 294 ms e a copia medida continuou
  perto de 0.10 ms. ADR-0005 torna CUDA Graph opt-in no servidor OFX.
- ADR-0005 foi implementado e o harness RPC Green 2048 confirmou o novo default
  com `cuda_graph_env=0`, `torchtrt_forward_direct` em cerca de 300 ms e
  `torchtrt_forward_direct_queue_wait` em cerca de 7 ms. O proximo Resolve run
  deve validar o mesmo sinal no host real.
- A validacao Resolve do pacote `0.8.5-win.1-74-g2c57c94`, depois de limpar as
  variaveis de ambiente, confirmou `cuda_graph_env=0` e
  `torchtrt_cuda_graph_input_copy_queue_wait=0`, mas o tempo ainda ficou em
  cerca de 1.9 s. O gargalo remanescente agora esta isolado em
  `torchtrt_forward_direct_queue_wait`: cerca de 901 ms no Resolve contra cerca
  de 7 ms no harness com as mesmas configuracoes efetivas.
- Quando o harness replica Lanczos4 e Source Passthrough efetivo 12/28, o
  `post_gpu_prepare` fica em cerca de 135 ms, proximo dos cerca de 141 ms do
  Resolve. Portanto esse custo e secundario e mensurado, nao o gargalo P0.
- A medicao atual ainda agrega coisas diferentes em
  `torchtrt_forward_direct_queue_wait`. O padrao de referencia do TensorRT
  separa `Enqueue Time` de `GPU Compute Time`; por isso o proximo slice precisa
  separar o tempo de `module.forward` no host do tempo bloqueado em
  `cudaEventSynchronize` e o excedente desse sync acima do tempo GPU medido.

## Hipoteses Testaveis

0. Fronteira de stream do input TorchTRT e o bloqueio P0.
   Quando o preparo CUDA/NPP roda em uma stream independente e o TorchTRT consome
   via evento, o Resolve atrasa a conclusao observada pelo consumidor em cerca
   de 0.8 a 1.4 s, embora o trabalho GPU medido dure cerca de 7 a 13 ms.
   Hipotese: enfileirar o preparo na current stream do Torch/PyTorch elimina o
   wait cross-stream sem voltar ao roundtrip de host da `main`.

0.1. Medicao de readiness nao deve sincronizar o host.
   Quando a dependencia ja esta ordenada pela mesma stream ou por
   `cudaStreamWaitEvent`, bloquear o host em `torchtrt_input_ready_wait` apenas
   para medir readiness reintroduz o mesmo gargalo. Hipotese: manter a ordem no
   grafo CUDA e reportar readiness wait como zero remove esse bloqueio.

0.2. A default current stream do PyTorch sob Resolve cria parte da fila do
   static input copy.
   Quando `getCurrentCUDAStream()` e usado sem `CUDAStreamGuard`, o PyTorch
   entrega a default stream. A work stream propria do TorchTRT remove os waits
   anteriores, mas a validacao real ainda mostra fila no static input copy.

0.3. CUDA Graph static input copy interage mal com o contexto Resolve.
   Com GPU prep e input readiness zerados, o wait restante ocorre antes do
   replay e nao no device copy. Hipotese: uma comparacao Resolve com CUDA Graph
   off e uma comparacao diagnostica com roundtrip host estilo `main` isolam se
   o problema e especifico do graph, do contexto host, ou de outra dependencia
   ainda nao instrumentada.

0.4. Direct forward pode estar enqueue-bound ou sync-bound.
   `torchtrt_forward_direct_queue_wait` hoje e `wall - gpu_event`. Hipotese:
   separar `torchtrt_forward_direct_enqueue_wall` e
   `torchtrt_forward_direct_event_sync_over_gpu` mostra se o gargalo esta na
   chamada TorchTRT/driver que enfileira kernels ou no host esperando o evento de
   fim do stream sob o contexto Resolve.

1. Auto Despeckle ativa CPU demais.
   Quando `auto_despeckle` esta ligado, o TorchTRT desliga todo o post-process
   GPU, nao apenas o despeckle. Hipotese: manter Source Passthrough, Despill e
   clamp em GPU e executar somente despeckle em CPU reduz E2E sem alterar
   qualidade visual.

2. Source Passthrough ainda paga copia evitavel em alguns caminhos.
   Quando a imagem fonte ja esta em GPU via preparo CUDA, o caminho deve usar
   device-to-device. Hipotese: casos que caem em host-to-device indicam fallback
   ou tamanho/canal incompativel que precisa aparecer em telemetry.

3. Cliente OFX pode ser gargalo depois do broker.
   O broker evita writeback duplicado quando `external_output_written` e true,
   mas o cliente ainda copia do shared frame para `ImageBuffer`. Hipotese: esse
   custo e aceitavel para cache/ajustes/escrita OFX, mas precisa de numero por
   caso antes de qualquer mudanca estrutural.

4. Casos constantes escondem trabalho real.
   Inputs constantes podem mascarar Source Passthrough, Despeckle e composicao.
   Hipotese: casos `plate` e `random` no harness expõem gargalos que testes
   constantes nao expõem.

5. Politica de shared nodes precisa proteger parametros de inferencia.
   Multiplos nos em Resolve/Fusion e Nuke compartilham cache. Hipotese: Screen
   Color, qualidade, resolucao efetiva, Source Passthrough, Despill, Despeckle,
   Upscale e Tiling devem sincronizar quando ha shared nodes; output mode e
   ajustes locais de alpha podem permanecer por instancia quando nao quebram o
   cache compartilhado.

## Trabalho Restante

- P0: Separar e reduzir o `torchtrt_forward_direct_queue_wait` especifico do
  Resolve, preservando `cuda_graph_env=0`, `torchtrt_input_ready_wait=0` e
  `gpu_prepare_wait_over_device=0`.
- P1: Depois do P0, otimizar Source Passthrough 12/28 e output copies apenas
  como custos secundarios ja mensurados.
- P0: Manter o caminho device-input e o device-to-device de Source Passthrough
  quando `source_rgb_device` esta disponivel.
- Separar o status `post_processed` em flags por etapa ou outro contrato
  equivalente, para permitir post-process parcial em GPU quando Despeckle
  ainda precisar de CPU.
- Medir e comparar `post_source_passthrough_gpu`, `post_despill_gpu`,
  `post_despeckle`, `torchtrt_output_d2h_direct`, `ofx_client_*_readback` e
  `ofx_write_output` em Green 2048 com parametros ligados e desligados.
- Validar que Source Passthrough usa device-to-device quando `source_rgb_device`
  esta disponivel e que fallback host-to-device fica visivel no log.
- Confirmar que Blue e Blue-Green preservam os mesmos ganhos quando o caminho
  tecnico permite, sem trocar o contrato de artefato definido para cada modo.
- Rodar a matriz TorchTRT com casos `plate` e `random` antes de pedir teste
  manual.
- Rodar gates de release pelo wrapper canonico `scripts/windows.ps1`, nunca por
  scripts internos diretos.

## Gates Para Fechar

- `git diff --check`
- compilacao canonica Windows via `scripts/windows.ps1`
- unit, regression e integration tests relevantes
- TorchTRT readiness matrix com Green, Blue e Blue-Green em 2048
- validacao de bundle, doctor, inventory e manifesto
- instalador online com label unico derivado da worktree
- UAT manual apenas para Resolve/Nuke quando os gates automaticos nao
  conseguirem substituir host real
