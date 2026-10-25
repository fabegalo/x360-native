# Full resolve fixed16 — 0201

Base local: `c6f091e`, preservando 0199/0200 e os commits anteriores. SDK
mantido em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.

## Causa comprovada

Os shaders full incorporados ao SDK ainda usavam `32/32767`
(`0x3A800100`) e `max(-1, valor)` ao desempacotar EDRAM signed16. O produtor
ROV/FSI já usa s5.10 desde 0185. Os helpers Canary em `pixel_formats.xesli`
usam extensão de sinal e `1/1024` (`0x3A800000`), preservando negativos abaixo
de −1. O código-fonte correto já acompanhado por 0199 não atualizava os
bytecodes full antigos, que continuavam sendo utilizados para fixed16.

A comparação local incluiu código-fonte, disassembly DXBC/SPIR-V e readbacks
reais. No caso 1×, fonte 16_16, destino RGBA32_FLOAT, bias 0, pixel (8,0),
os códigos de entrada são `[1023,-32767]`: o readback antigo informa
aproximadamente `[0.999054, -1, 0, 0]`; o valor s5.10 exigido é
`[0.9990234375, -31.9990234375, 0, 0]`. A comparação efetiva verifica os bytes
integrais, incluindo padding, e não uma tolerância decimal.

Reprodução com os recursos antigos: **48 falhas / 54 casos por API**, saída 1.
Uma experiência local substituindo somente a escala continuou falhando por
causa do clamp em −1. Esse candidato que alterava somente a escala foi rejeitado.
O candidato final elimina as 48 falhas em D3D12 e em Vulkan, preservando os
seis casos que já coincidiam devido à conversão de destino.

## Correção

0201 adiciona kernels full especializados para fontes EDRAM 16_16 e
16_16_16_16, usando os helpers s5.10 de
[Canary 534aeb64](https://github.com/xenia-canary/xenia-canary/commit/534aeb64ea1937361573e3933d121e636463b135).
Reutiliza o endereçamento SDK, os blocos escalados e a acumulação precisa de
0199, com reutilização de endereços HLSL de 0200. As seis funções de unpack
são especializadas por pré-processamento; não há instrumentação no runtime.

A recompilação de todos os kernels passou numericamente, mas regrediu no
microbenchmark. A versão final usa **oito recursos regenerados do IR pinado**
para destinos de 16/32/64/128 bpp sem escala. Neles, somente o multiplicador
do unpack e seu limite inferior mudam: DXBC usa −32, mínimo exato s5.10,
e checksum recalculado; SPIR-V usa `FMax(x,x)` no lugar do clamp de origem.
As instruções, conversões e clamps de destino restantes são preservados e
conferidos palavra a palavra. Os **12 recursos restantes** (R8 escalar e
variantes escaladas) são compilados da fonte corrigida para cobrir também
as diferenças de seleção escalar e endereçamento demonstradas pela matriz.

São **38 arquivos SDK**: 20 novos recursos DXBC/SPIR-V, dez wrappers XESL,
`resolve.xesli`, README, dois manifests e quatro arquivos dos caches
D3D12/Vulkan. Cada cache ganha cinco pipelines privados para fixed16. O enum
compartilhado, constantes de 20/16 bytes, descritores e cópias raw permanecem
compatíveis. A seleção é restrita a fontes fixed16 em full resolve; RGB565
continua explicitamente nos pipelines antigos. Native mantém seus caminhos.

Os 20 recursos full originais, os 20 gamma e seus dez wrappers foram
conferidos contra a base. Os bytecodes dessas duas famílias são idênticos.
Os manifests gamma foram atualizados pelos novos hashes de fontes/ferramenta;
as regressões gamma resolvidas não foram reinvestigadas.

`scripts/shaders/generate_resolves.py --family fixed16 --dxbc-checksum <tool>`
reutiliza FXC, glslang pinado, SPIRV-Tools locais e o checksum DXBC do SDK.
O tool é compilado com o target `x360n_resolve_dxbc_checksum`; o manifest
registra hashes dos inputs pinados e a lista exata de operandos alterados.
Geração e `--verify` reproduziram os 20 recursos
exatos; `spirv-val --target-env vulkan1.0` aprovou os dez módulos. A geração
gamma também passou em `--verify`, conservando seus recursos. Builds normais
consomem os headers versionados e dispensam compiladores de manutenção.

## CPU, GPU e compatibilidade

- **CPU: 291.132/291.132 verificações**, incluindo a função real de unpack
  extraída de `pixel_formats.xesli` contra interpretação signed racional
  independente: todos os 65.536 códigos em quatro lanes, com bits superiores
  distintos. Os 28.980 checks gamma anteriores permanecem aprovados.
- **Onze regressões focadas aprovadas**, cobrindo fixed16/raw, ROV/FSI de 0185,
  MSAA de 0197, window offsets de 0198, aliases, destinos/slices,
  DXBC/SPIR-V, sincronização de uploads e gamma de 0199/0200. O novo
  `sdkcheck.fixed16-full-resources` verifica proveniência, hashes, ausência da
  escala antiga e preservação dos demais operandos do IR. Falha antes de 0201.
- **RTX 4070: 2.052 casos por API**, usando os recursos incorporados ao
  Release, com upload, dispatch e readback. **864 fixed16/API sem divergências**:
  os 54 casos originais e 810 adicionais. **716 não FP16** têm bytes idênticos
  entre APIs; **148 FP16** são exatos ao oráculo de sua API, sendo 142 distintos
  entre APIs pela conversão nativa DXBC RTZ / SPIR-V RTE.
- A matriz cobre EDRAM 32/64 bits, negativos, zero, ±1 LSB, limites
  −32768/32767, MSAA 1×/2×/4×, seleções individuais/pares/todas válidas,
  bias −32/−16/−8/−1/0/2/8/+31, escala 1×/2×/3×/4×, swap, endian,
  offsets parciais, wrapping e slices. Destinos: R8, RG8, RGBA8,
  RGB10A2, RGBA16_FLOAT e RGBA32_FLOAT. Bias +31 não é aplicado ao FP16;
  overflow FP16 continua fora desta matriz, com o intrinsic da API preservado.
- **684 gamma/API** exatos: 589 não FP16 idênticos entre APIs e 95 FP16
  conferidos segundo RTZ/RTE. **504 controles/API** permanecem byte a byte
  iguais ao readback dos recursos SDK originais.
- **45 diferenças RGB565/API preservadas e separadas**: nove com fonte 64bpp
  sem swap, 18 com swap/fonte 32bpp e 18 com swap/fonte 64bpp. Não há correção
  nem novo aceite de fidelidade desse formato.

A ampliação demonstrou dois casos não cobertos pelo oráculo anterior:
alpha ausente em resolve **escalar** R8 de fonte 16_16 é 1 antes da média/bias;
endian em destinos de 16 bits aceita somente 8in16, enquanto destinos de
8 bits ignoram endian. Ambos foram demonstrados pelo código Canary e pelos
readbacks; os asserts CPU registram essas regras. O conversor FP16 independente
também trata explicitamente underflow abaixo de meio LSB, evitando shifts
inválidos nos novos biases negativos. Nenhum esperado dos 48 casos foi
alterado para aceitar o comportamento antigo.

Release final de **rexgpu-xenos e rexgpu-native aprovado**, com D3D12/Vulkan
habilitados, além das sondas CPU/GPU. O replay limpo aplicou **188 patches** e
conferiu **256 hashes finais**, incluindo todos os patches/hashes anteriores.
O verificador do checkout e a auditoria de identidade dos recursos passaram.
Não foram executadas suítes completas.

## Medição

640×384, RGBA8, escala 1×, entrada residente, 30 lotes de aquecimento e
100 medidos, 256 resolves ordenados por lote, três ensaios pareados com ordem
alternada. RTX 4070 nas duas APIs, timestamps GPU e percentis nearest-rank;
abaixo, mediana dos percentis de cada ensaio. Frequências não foram fixadas;
a versão do driver não foi coletada nesta rodada. A preparação e submissão
CPU são amortizadas por lote e estão no CSV.

O fixture de desempenho usa códigos positivos constantes
`[1024,512,256,2048]` em todas as amostras, para obter a mesma saída RGBA8
antes/depois, conferida pelo oráculo. Com fonte 32bpp, os componentes ausentes
são zero. Os casos negativos e demais limites são cobertos pela matriz GPU.
Os mesmos buffers de teste somam 328 MiB + 16 bytes em ambas as variantes;
o dimensionamento permite a matriz até escala 4×. Upload inicial e criação de
pipelines ficam fora da janela medida. Os cinco PSOs privados adicionais no
renderer não tiveram seu custo de inicialização/memória medido.

| Fonte / API / MSAA | GPU P50 antes → depois (µs) | P95 | P99 | Δ P50 |
|---|---:|---:|---:|---:|
| 32 / D3D12 / 1× | 4.060 → 4.064 | 4.808 → 4.904 | 5.012 → 5.076 | +0.10% |
| 32 / D3D12 / 2× | 4.660 → 4.660 | 4.976 → 4.688 | 5.412 → 4.688 | +0.00% |
| 32 / D3D12 / 4× | 6.548 → 6.456 | 6.736 → 7.228 | 6.744 → 7.328 | -1.41% |
| 32 / Vulkan / 1× | 2.786 → 2.753 | 7.520 → 7.548 | 7.532 → 7.556 | -1.21% |
| 32 / Vulkan / 2× | 3.482 → 3.385 | 4.269 → 6.352 | 4.368 → 9.840 | -2.77% |
| 32 / Vulkan / 4× | 5.092 → 5.023 | 12.848 → 12.656 | 13.384 → 12.880 | -1.35% |
| 64 / D3D12 / 1× | 4.308 → 4.300 | 5.064 → 4.496 | 6.212 → 6.196 | -0.19% |
| 64 / D3D12 / 2× | 5.760 → 5.140 | 6.520 → 6.768 | 6.912 → 7.224 | -10.76% |
| 64 / D3D12 / 4× | 12.792 → 12.784 | 14.440 → 14.452 | 15.104 → 15.132 | -0.06% |
| 64 / Vulkan / 1× | 3.085 → 3.021 | 8.004 → 8.172 | 10.524 → 8.336 | -2.07% |
| 64 / Vulkan / 2× | 4.918 → 4.880 | 11.740 → 4.988 | 11.780 → 6.760 | -0.77% |
| 64 / Vulkan / 4× | 14.208 → 14.248 | 26.600 → 23.436 | 28.300 → 25.780 | +0.28% |

O P50 ficou entre −10,76% e +0,28% nesta coleta. As caudas são mistas:
Vulkan/fonte 32bpp/2× teve P99 de 4,368 → 9,840 µs. Três pares, clocks
dinâmicos e a dispersão observada não estabelecem ganho global ou ausência
de regressão de cauda. CPU prepare P50 variou entre −5,55% e +5,38%; submit,
entre −5,00% e +5,32%. Esses números não representam FPS, o custo completo do
RenderTargetCache nem criação de PSOs.
[CSV final com CPU/GPU e todos os percentis](full-resolve-fixed16-0201-2026-10-25.csv).

O candidato com todos os kernels recompilados passou numericamente e foi
rejeitado por desempenho: Vulkan P50 subiu +25,26% a +53,68% na fonte 32bpp
e +2,46% a +44,36% na fonte 64bpp. D3D12 teve casos sem ganho, incluindo
+28,75% em 32bpp/1× e +14,23% em 64bpp/1×. Esses resultados permanecem no
[CSV do candidato rejeitado](full-resolve-fixed16-0201-compiled-candidate-2026-10-25.csv).
Uma rodada diagnóstica com replay concorrente foi excluída das duas tabelas.
As medições finais ocorreram sem builds, replay ou outros testes GPU concorrentes.

Reprodução: `benchmark_gamma_resolve.py --fixed16 --trials 3 --reference <originais>
--candidate <finais> --json <resultado>`; repetir com `--source64`.
Os JSONs aceitos são `performance-accepted-32.json` e
`performance-accepted-64.json`. Os JSONs `performance-final-32.json` e
`performance-final-64.json` registram o candidato recompilado rejeitado.

## Reprodução e limites

Evidências locais em `.local/patch-work/0201/`: `red-final-gpu.log` (entrada,
bytes divergentes, valores numéricos e saída 1), `gpu-accepted.log` (matriz
integrada final e saída 0), `cpu-final.log`, `ctest.log`,
`ctest-resources-final.log`, `release-accepted.log`,
`repro-accepted-fixed16.log`, `repro-accepted-gamma.log`, `replay-accepted.log`
e `audit.json`. `final-binaries` é conferido contra os hashes dos headers
finais e fornece o candidato das medições aceitas.

```powershell
# Build Release via devshell, incluindo ambas as APIs e as sondas.
scripts/devshell.cmd cmake --build --preset win-amd64-release --target rexgpu-xenos rexgpu-native x360n_gamma_resolve_checks x360n_gamma_resolve_gpu_checks
.local/build/win-amd64/tests/sdkcheck/g3/Release/x360n_gamma_resolve_checks.exe
# Extrair baseline pinado, sem rede.
.local/toolchains/python-3.14.8/python.exe tests/sdkcheck/extract_resolve_baseline.py --output .local/resolve-original
# Red: saída 1, 48 falhas/API nos 54 casos originais.
.local/build/win-amd64/tests/sdkcheck/g3/Release/x360n_gamma_resolve_gpu_checks.exe --fixed16-only --shader-dir .local/resolve-original
# Green + controles/gamma: saída 0, 2.052 casos/API.
.local/build/win-amd64/tests/sdkcheck/g3/Release/x360n_gamma_resolve_gpu_checks.exe --extended-fixed16 --baseline-dir .local/resolve-original
```

Os processos de medição têm timeout de 90 s por sonda e limites fixos de
iterações; fences GPU têm timeout de cinco segundos. As validações são compute
headless e não certificam rasterização completa, gameplay ou equivalência com
hardware original. Khronos validation layers estavam indisponíveis; validation
layers e synchronization validation não foram executadas. C7/C8, perfis,
saves, biblioteca e isolamento de versões não foram alterados. Nenhum jogo,
Computer Use, push, tag ou publicação de Release foi executado.
