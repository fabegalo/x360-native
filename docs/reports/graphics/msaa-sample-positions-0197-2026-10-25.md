# Xenos MSAA — patch 0197

Base local/remota: `main` `b3bec63b725282e0019c9b24c2fab4aee13a2e7b`.
O incremento 0196 e todas as alterações locais anteriores foram preservados.
SDK: `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, sem atualização.
Referência: [Canary cc4981a4659086acb1c25a485adebefc3f432ae1](https://github.com/xenia-canary/xenia-canary/commit/cc4981a4659086acb1c25a485adebefc3f432ae1).

## Implementado

- Posições Xenos, em unidades de 1/16 pixel: 2× `(-4,4),(4,-4)`;
  4× `(-2,-2),(2,2),(-6,6),(6,-6)`.
- Pareamento host/guest de 2× nativo e 2× emulado como 4×. ROV/FSI passam a
  obter cobertura de host 2/1 no 2× emulado e 0/3/2/1 no 4×. Isso substitui
  a regra do 0188. Depth é extrapolado para a posição Xenos, mantendo os
  índices lógicos da EDRAM.
- D3D12: sample positions por CommandList1 e tier; programação/reset conforme
  MSAA real da pipeline, clear inicial de depth sob o padrão correspondente,
  transições centralizadas e decompress antes de ler depth multisampled.
- Vulkan: detecção/habilitação de `VK_EXT_sample_locations`, sample counts e
  `variableSampleLocations`; posições em pipelines, render passes e barreiras
  depth. Os comandos diferidos copiam os arrays e reconstroem os `pNext` no
  replay. Scratch é local ao replay, preservando o worker do 0157.
- Host RT usa render pass quando o padrão exige os metadados depth da extensão;
  os demais caminhos conservam dynamic rendering. Ao alternar, o pass anterior
  é fechado pela sua própria API. O cache de viewport existente incorpora MSAA;
  escalonamento preserva a fração de pixel correspondente à amostra.
- Corrigida uma divergência adicional local: A2C SPIR-V de host RT 4× trocava
  os índices 1/2. Agora concorda com os índices dos loads/stores, das posições
  programadas e de D3D12. FSI conserva a remoção dos bits de cobertura e de
  escrita depth/stencil diferida.

Como no commit Canary, `xenos_sample_positions=false` por padrão deixa a
programação de **host RT** opt-in. ROV/FSI usam depth Xenos independentemente
dessa opção, com cobertura das amostras host mais próximas no fallback.
Dispositivo sem recurso opcional conserva rasterização padrão; isso não torna
a cobertura de bordas idêntica à do console. MSAA 1× permanece com padrão normal.

O patch modifica 23 arquivos SDK: command processors, caches RT/pipeline e
listas/buffers diferidos D3D12/Vulkan; utilitários draw; tradutores OM DXBC/RB
SPIR-V; flags compartilhadas; device Vulkan. Integração em `deps.lock.json`.
Regressões em `tests/sdkcheck/{canary_msaa_probe,msaa_gpu_probe}.cpp`,
`g3/{spirv_canary,dxbc_canary}.cpp`, captura/generator de upload e CMake g3.
Não foram modificados fontes Native, FPS, vblank, física ou resolução original.

## Evidências

| Verificação | Resultado |
|---|---|
| SPIR-V antes do port | 2 falhas demonstradas, 9/11 checks |
| A2C antes da correção dos índices locais | 4 falhas demonstradas, 181/185 checks |
| Posições/mapeamentos/viewport CPU | 54 checks, zero falhas |
| SPIR-V offline | 185/185; tabelas de cobertura para todas as 16 máscaras, 2× nativo/emulado, escalas 1/2, A2C 1×/2×/4×, FSI/host RT |
| DXBC offline | 16/16; RTV/ROV traduzidos e aceitos por D3DDisassemble disponível no sistema |
| Captura Vulkan de código de produção | 18 checks; uploads repetidos, arrays/pNext próprios, TakeStreams e troca de API de pass |
| Interlock/rectangle CFG | 60 + 30 casos aprovados |
| CTest focado Release | 9/9, 1,39 s; inclui hazards, resolve, viewport e EDRAM alias |
| Build Release | `rexgpu-xenos` D3D12/Vulkan e todos os probes afetados: saída 0, com warnings |
| Verificação oficial SDK/lock | Saída 0 |
| Replay isolado | 184 patches, 180 hashes finais idênticos; pin preservado |

GPU: NVIDIA GeForce RTX 4070, execução headless com limite externo de 45 s e
espera por fence limitada a 5 s por cenário. D3D12 informou tier 2 e concluiu
clear/transição depth em 1× padrão e 2×/4× programados, incluindo decompress.
Vulkan com features Xenos expôs sample locations/variable locations e concluiu
1× padrão e 2×/4× programados. Um device Vulkan criado sem features Xenos
também concluiu os três modos no fallback; esse resultado **não** indica
ausência da extensão no hardware. Não foi repetido o smoke D3D12 após sua
aprovação; os ajustes seguintes afetaram a composição de passes do backend.

Esses smokes executam o helper real de posições e as APIs de clear/transição;
não executam uma cena do renderer Xenos nem comparam pixels ou amostras lidas.
Não certificam rasterização/A2C na GPU. Vulkan validation foi solicitado na
criação da instância, mas a presença da layer e synchronization validation não
foram certificadas. `spirv-val` não estava disponível no PATH/toolchain; os
testes SPIR-V são tradução e inspeção/avaliação do programa emitido, não uma
aprovação formal desse validador. D3DDisassemble também não é um teste de pixels.

Os primeiros builds detectaram diferenças de contadores entre os projetos e
erros de enum/consulta no fixture; todos foram corrigidos antes do build final.
O replay usa `core.autocrlf=false`, como o SDK, e blobs LF; preserva os hashes
históricos de base e o warning de whitespace do patch 0114.

Logs locais: `.local/patch-work/0197/{red-test,a2c-red,tests-final,gpu1,gpu-xenos,
build-complete,capture-final-build,sdk-check-final,replay-final}.log`.

Pendentes: cenas sintéticas rasterizadas com readback, comparação visual no
renderer completo, synchronization validation, outros dispositivos/drivers e
gameplay autorizado. Sem ganho de performance demonstrado, jogos comerciais,
Computer Use, suíte completa, commits, push, tags ou Release nesta etapa.
