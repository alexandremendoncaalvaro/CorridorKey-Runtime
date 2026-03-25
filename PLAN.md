## Corrigir falha RTX em `1536/2048` com erro explícito e probes reais de TensorRT

### Resumo
Os logs deste usuário mostram um padrão determinístico: o TensorRT RTX falha na criação do engine para `corridorkey_fp16_2048.onnx` e `corridorkey_fp16_1536.onnx`, depois compila e renderiza com sucesso em `corridorkey_fp16_1024.onnx`. Não há evidência de `OOM`; a falha é de compilação do EP (`NvTensorRTRTX EP failed to create engine from network`) e hoje o OFX repete esse ciclo muitas vezes, porque não memoriza que as resoluções maiores já falharam.

### Mudanças principais
- Ajustar o fluxo de qualidade no OFX em [`/Volumes/MacMini/Home/Dev/Repos/CorridorKey-Runtime/src/plugins/ofx/ofx_instance.cpp`] para:
  - Modos fixos `Ultra (1536)` e `Maximum (2048)` no backend TensorRT RTX falharem explicitamente quando o engine da resolução pedida não compilar.
  - `Auto` continuar fazendo downgrade para a maior resolução funcional, mas sem reexecutar tentativas já conhecidas como inválidas na mesma instância/sessão.
- Adicionar estado interno em [`/Volumes/MacMini/Home/Dev/Repos/CorridorKey-Runtime/src/plugins/ofx/ofx_shared.hpp`] para cache negativo de compilação TensorRT por artefato/resolução/backend/dispositivo, invalidado quando mudarem dispositivo, backend, quantização, bundle de modelos ou a instância do plugin.
- Manter o painel runtime coerente:
  - Modos fixos exibem erro claro com o artefato que falhou.
  - `Auto` exibe nota de fallback, mas sem thrash de recompilação.
- Estender o `doctor` em [`/Volumes/MacMini/Home/Dev/Repos/CorridorKey-Runtime/src/app/runtime_diagnostics.cpp`] para incluir probes estritos de TensorRT RTX nos modelos FP16 empacotados (`512/768/1024/1536/2048`) com `disable_cpu_ep_fallback=1`, registrando quais resoluções realmente criam sessão e executam frame.
- Preservar compatibilidade do JSON atual do `doctor`: não renomear a chave `windows_universal`; só enriquecer os probes e corrigir as razões/recomendações para refletirem TensorRT quando aplicável.
- Não mudar a API pública da biblioteca em `include/corridorkey/`; a mudança é comportamental no OFX e diagnóstica no app/CLI.

### Interfaces e comportamento público
- OFX:
  - `Maximum (2048)` e `Ultra (1536)` deixam de cair silenciosamente para resolução menor no TensorRT RTX; passam a falhar com mensagem explícita quando a compilação do engine falhar.
  - `Auto` continua funcional com downgrade automático.
- `doctor`/CLI:
  - `execution_probes` passa a incluir entradas reais de TensorRT RTX.
  - `recommended_model` para Windows RTX passa a ser baseado em probe estrito do backend principal, não só em presença de arquivos/providers.

### Testes
- Adicionar regressão cobrindo que modo fixo TensorRT não tenta candidatos inferiores após falha do artefato exato.
- Adicionar regressão cobrindo que `Auto` faz downgrade uma vez e reutiliza a resolução funcional sem reabrir `2048/1536` repetidamente.
- Adicionar teste para invalidação do cache negativo quando mudar backend/dispositivo/modelos.
- Atualizar testes do `doctor` para aceitar probes TensorRT no bloco `windows_universal` e validar recomendação baseada em execução estrita.
- Se houver harness viável, adicionar integração do broker/runtime client simulando `2048 -> fail`, `1536 -> fail`, `1024 -> success`.

### Premissas
- Vou assumir que o segundo usuário apresenta a mesma classe de falha até aparecer log contrário.
- Vou tratar a causa raiz imediata como incompatibilidade de compilação do TensorRT RTX para esses grafos/builds, não como erro de detecção de VRAM.
- Este plano melhora comportamento e diagnósticos; ele não promete fazer `1536/2048` compilarem no backend atual. Se quisermos atacar isso depois, o próximo passo é um track separado de validação de ONNX Runtime/TensorRT/compiled context nesses dois modelos.
