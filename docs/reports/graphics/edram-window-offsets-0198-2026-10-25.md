# 0198 — offsets de tiles EDRAM

HEAD local/main e origin/main verificados no início: `b3bec63b725282e0019c9b24c2fab4aee13a2e7b`.
0196/0197 já estavam no checkout, sem commit; foram preservados. Nenhum commit,
push, tag ou Release criado. SDK permanece em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.

## Implementação

`0198-canary-edram-window-offsets.patch` adapta o código de
[Canary e1f4a0d](https://github.com/xenia-canary/xenia-canary/commit/e1f4a0d00968ae9ea1d667f28912cc051a5f637f).
Modifica 18 arquivos SDK: draw/extent, RenderTargetCache comum e dos dois
backends, command processors e contratos/tradutores DXBC/SPIR-V.

- Offsets negativos de tiles inteiros passam às bases EDRAM; zero, positivos e
  offsets parciais mantêm o caminho anterior. Host RTV/FBO admite deslocamento
  vertical elegível; ROV/FSI admite também horizontal e wrapping.
- Bases guest originais continuam sendo usadas nas comparações de aliases.
  Bases host ajustadas, início do scissor, ownership e transferências usam o
  mesmo espaço. 64bpp duplica o deslocamento; a chave e os shaders de transferência
  representam o segundo período sem confundi-lo com o primeiro.
- Recusas de deslocamento ficam estáveis por offset/pitch/MSAA entre passes e
  frames relacionados. Mudanças de mapeamento interlock pedem barrier.
- Viewport, scissor e DrawExtentEstimator aplicam a transformação uma vez.
  PsParamGen recompõe XY original após desfazer a escala, antes de abs/flags.
  Constantes foram acrescentadas preservando os campos anteriores do SDK.
- Preservados MSAA 0197, sincronização 0196 e o cache de viewport D3D12 local.
  Não foram importadas funcionalidades Canary independentes, como scale_native
  e sua política separada de depth aliasing RTV, ausentes neste SDK.

Integração: `deps.lock.json`; testes em `tests/sdkcheck/g3/{edram_window_offsets,
spirv_canary,dxbc_canary}.cpp`, `edram_window_gpu.cpp`, CMake g3 e runner de
benchmark. Fontes Native, resolução, física, FPS e vblank não foram alterados.

## Verificação

| Evidência | Resultado |
|---|---|
| Controle negativo com fontes/headers 0197 isolados | SPIR-V 185/186; DXBC 18/20, falhando na soma do offset PsParamGen |
| CPU comum real | 201 checks: 1×/2×/4×, offsets X/Y e sinais, viewport/scissor/extent, 32/64bpp, wrapping/segundo período, aliases, prepass depth/stencil, ownership, transferências e barriers |
| Tradução atual | SPIR-V 189/189, incluindo PsParamGen FBO/FSI e escalas 1/2; DXBC 20/20, RTV/ROV aceitos por D3DDisassemble |
| CTest focado | 10/10: inclui resolves, MSAA, aliases, CFG interlock e capturas de sincronização Vulkan |
| Release | `rexgpu-xenos` com D3D12/Vulkan e probes afetados aprovados; warnings existentes |
| Verificador oficial SDK/lock | `apply-sdk-patches.ps1 -Check`, saída 0 |
| Replay isolado desde o pin | 185 patches; 182 hashes finais idênticos |

SHA-256 do patch:
`0b4f4eaab6125be2d94949be191b9af34c46d8290f75169c876a740574ba0f87`.
Mantidos hashes históricos de base; o warning de whitespace do 0114 permanece.

GPU RTX 4070, headless: 11 cenários, cada um com 10.240 pixels comparados.
D3D12 tier 2: 1× padrão, 2×/4× padrão e programados. Vulkan: mesmos modos,
mais 2× guest emulado em 4× host. Fullscreen depth prepass, draws subsequentes
em tiles, resolve e readback conferem as coordenadas esperadas. Omitir a
transformação no controle negativo causa falha de readback. Limites: 45 s por
processo e 5 s por fence. Corrigida a liberação prematura da DLL do compilador
no próprio probe, antes da execução final aprovada.

Esse probe usa helpers reais de transformação/posições com shaders de teste.
Não executa o command processor Xenos completo nem os shaders ROV/FSI completos;
não certifica rasterização de bordas/A2C, stencil ou transferências de aliases
na GPU. Khronos Validation não está instalada: validation/synchronization
validation foram solicitadas, mas não executadas. `spirv-val` indisponível;
inspeção SPIR-V e compilação GLSL do probe não equivalem à sua validação formal.

## Medição e decisão

Ryzen 7 5700X3D, Release LLVM 22.1.8, afinidade de processo `0x4`.
Runner versionado reconstrói fontes/headers 0197 numa cópia isolada, confere os
hashes e compara o RenderTargetCache real. Não altera o SDK integrado.
10 lotes de aquecimento + 200 lotes de 1.000 draws; mediana dos percentis de
três rodadas alternando a ordem. Valores em ns/draw, offset + atualização do cache:

| Cenário | P50 | P95 | P99 |
|---|---:|---:|---:|
| 0197 ROV/FSI, zero | 90,5 | 170,9 | 179,2 |
| 0198 ROV/FSI, zero | 102,7 | 186,4 | 194,2 |
| 0197 RTV/FBO, zero | 89,9 | 102,1 | 158,6 |
| 0198 RTV/FBO, zero | 102,2 | 191,3 | 195,1 |
| 0198 ROV/FSI, tile estável | 108,6 | 117,1 | 146,9 |
| 0198 RTV/FBO, tile estável | 109,9 | 122,6 | 229,7 |
| 0198 ROV/FSI, alternando offset | 288,4 | 303,1 | 359,3 |
| 0198 RTV/FBO, alternando offset | 187,6 | 199,3 | 214,9 |

Há custo adicional medido de ~12 ns no P50 sem offset; nenhum ganho global é
alegado. Caudas variaram com a atividade do sistema. O controle 0197 compara
somente zero offset, cuja semântica é equivalente. Tiles não são comparação
de performance com a implementação anterior incorreta.

Nos 210.000 draws por cenário, tiles estáveis geraram zero transferências e
barriers adicionais, com dois recursos host RT criados. Alternância exigiu
209.999 barriers interlock ou 419.998 planos de transferência host RT e quatro
recursos. São contadores do planejamento real com backend de gravação, não
medidas de cópias/barriers executadas na GPU nem contagem global de heap.

Uma tentativa isolada de retorno antecipado no cache para offset zero foi
**rejeitada**. Em cinco rodadas, P50 zero melhorou (102,9→92,9 ns ROV/FSI,
106,1→99,7 ns RTV/FBO), mas tiles estáveis pioraram (108,4→117,3 e
109,9→121,2 ns). Não há patch 0199 ou otimização especulativa integrada.
Não foram removidas dependências de sincronização. Tempos separados de
submissão/GPU, alocações gerais e invalidações não foram instrumentados.

Reprodução após o build Release:

```bat
scripts\devshell.cmd .local\toolchains\python-3.14.8\python.exe tests\sdkcheck\benchmark_edram_window.py --trials 3
.local\build\win-amd64\tests\sdkcheck\g3\Release\x360n_edram_window_checks.exe --bench
.local\build\win-amd64\tests\sdkcheck\g3\Release\x360n_edram_window_gpu_checks.exe
```

Logs: `.local/patch-work/0198/{red-final,acceptance-tests,acceptance-gpu,
acceptance-lock,replay,benchmark-repro,compare-opt,build-final}.log`.
Pendente: renderer completo na GPU, validação formal SPIR-V, validation layers,
outros dispositivos/drivers, comparação visual e gameplay autorizado. Nenhum
jogo comercial foi iniciado; microbenchmarks não representam FPS de jogos.
