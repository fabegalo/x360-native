# Resolve escalado MSAA — 0202

Base `0f96a19`, incluindo `5b87e8c`; branch isolada `codex/resolve-msaa-0202`.
**120 falhas/API eliminadas**, sem alterar o oráculo RGB565 ou os bytecodes
anteriores. A integração está somente nesta branch, sem merge com N-PERF-01.

## Causa e correção

O shader antigo aplicava MSAA sobre coordenadas host já escaladas, misturando
subpixel e amostra. A transformação exata por eixo é
`((host / escala << bits_MSAA) + amostra) * escala + host % escala`.
No exemplo anterior (RGBA8, MSAA 2×, escala 2×, offset parcial, bias +1,
pixel 16,1), Y deve ser 33, não 34; o byte 2064 deve ser `23`, não `FF`.
Os 120 casos são 30 por combinação fonte 32/64bpp × swap on/off.

Consulta XERJ exclusivamente local e confronto dos fontes confirmaram a
separação de coordenadas do Canary e a reutilização de amostras de 0200.
Mantido o layout do SDK pinado: bit 0 da amostra é Y, bit 1 é X;
o interleave 4×4 mais novo do Canary não corresponde aos produtores deste SDK.

O novo helper calcula quatro endereços juntos, compartilha Y/tile e reutiliza
strides das outras amostras. Escalas 2/4 usam shifts; 3 usa divisão constante;
o resto vem de subtração, sem módulos. Wrapping ocorre antes de escalar o tile.
Cargas contíguas são agrupadas somente nas condições exatas de alinhamento.
D3D12 usa um kernel privado com cargas agrupadas; Vulkan mantém as cargas
escalares anteriores em um kernel privado para ambas as fontes. A seleção cobre
somente RGB565 UNORM escalado, MSAA 2/4, fontes RGBA8/RGBA16_FLOAT.
MSAA1, escala1, demais formatos, gamma 0199/0200 e fixed16 0201 conservam os
pipelines anteriores. Packing, bias, swap e destino tiled são reutilizados.

O candidato rejeitado repetia divisão/remainder de coordenadas e cálculo de
tiles por pixel/amostra. Seu SPIR-V tem 21 `OpUDiv` e 16 `OpUMod`, contra 6/0
no novo módulo Vulkan. A variante com apenas o endereço otimizado recuperou
o desempenho Vulkan32; impor as cargas vetoriais a essa fonte voltou a piorar
o resultado. A versão com pares vetoriais em fonte64 pareceu favorável em três
pares, mas seis pares adicionais expuseram P95/P99 +13,03%/+14,45% em MSAA4;
foi rejeitada ([CSV](scaled-msaa-resolve-0202-vector64-rejected-2026-10-25.csv)).
A versão escalar final passou em seis pares. Menos instruções totais não
garantiu menor latência; não se usou o candidato rejeitado como baseline.

## Validação

- Negativo reproduzido: 120/344 falhas por API, saída 1, recursos anteriores.
- Final RTX 4070 D3D12/Vulkan: **2.324 casos/API**, bytes completos;
  344 RGB565 passam (224 preservados, 120 corrigidos), 864 fixed16,
  684 gamma e todos os 504 controles originais preservados.
  As 45 diferenças R8 previamente registradas permanecem iguais e pendentes.
  Conversões FP16 RTZ/RTE continuam verificadas segundo cada API.
- Matriz: fontes 32/64, MSAA 1/2/4, escalas 1/2/3/4, seleções de amostra,
  bias, swap, endian, offsets parciais, pitch, slices e wrapping.
- CPU: 5.046.273 verificações do helper real contra oráculo físico uint64
  independente, 306.499 de formatos/quantização, 54 MSAA e 22 tiled-dest.
  Inclui escalas anisotrópicas, limites de tile e exclusões da seleção privada.
- Release: `rexgpu-xenos` compilado com **D3D12 e Vulkan habilitados** e sondas
  afetadas. Build isolado reutilizou dependências Release autenticadas por SHA;
  não recompilou o launcher ou o produto inteiro.
- Geração seguida de `--verify`: dois novos recursos exatos; gamma/fixed16
  também regenerados em temporários e comparados sem escrita. **76 recursos
  anteriores idênticos**. SPIR-V validado por `spirv-val`; layers Khronos ausentes.
- Replay limpo: **189 patches / 263 hashes finais**, pin preservado
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. Único aviso de whitespace
  pertence ao patch 0114 preexistente; 0202 aplica sem esse aviso.

## Medição

RTX 4070, driver 617.42, clocks dinâmicos, power limit 200 W; nenhuma sonda,
build ou replay concorrente durante medição. Mesmo protocolo de `0f96a19`:
640×384 guest, escala2, 983.040 pixels host, fixture uniforme conferido,
30 lotes de aquecimento + 100 medidos × 256 resolves ordenados;
três rodadas pareadas em fonte32 e seis em fonte64 MSAA2/4, ordem alternada,
percentis nearest-rank. Cada linha é a mediana dos percentis, em **µs**.
Baseline é o recurso anterior, funcional nos casos originais.

| Fonte/API/MSAA | P50 baseline → novo | P95 | P99 | Δ P50 |
|---|---:|---:|---:|---:|
| 32/D3D12/2 | 13,212 → 10,652 | 14,892 → 12,288 | 15,516 → 13,060 | −19,38% |
| 32/D3D12/4 | 25,620 → 15,160 | 27,392 → 16,892 | 27,828 → 17,504 | −40,83% |
| 32/Vulkan/2 | 10,952 → 10,448 | 24,058 → 21,264 | 25,260 → 23,332 | −4,60% |
| 32/Vulkan/4 | 15,808 → 14,532 | 27,020 → 25,172 | 29,188 → 27,264 | −8,07% |
| 64/D3D12/2 | 14,342 → 11,660 | 16,136 → 13,338 | 16,684 → 15,076 | −18,70% |
| 64/D3D12/4 | 38,624 → 34,654 | 40,762 → 36,536 | 41,160 → 36,996 | −10,28% |
| 64/Vulkan/2 | 12,888 → 12,666 | 22,766 → 22,124 | 24,428 → 23,466 | −1,72% |
| 64/Vulkan/4 | 39,664 → 39,013 | 46,628 → 46,542 | 48,242 → 47,592 | −1,64% |

[CSV](scaled-msaa-resolve-0202-2026-10-25.csv) registra os três candidatos,
MSAA1 e CPU preparação/submissão P50/P95/P99, em ns/resolve. CPU é custo da
sonda amortizado por lote, sem upload/criação de PSO. P50 preparação ficou
900–1.211 ns e submissão 165–242 ns. CPU P99 de submissão Vulkan64/MSAA2
variou 315 → 356 ns (+41 ns/resolve), com P50 igual e P95 menor; a rodada
vetorial anterior teve 307 → 325 ns, enquanto a primeira teve 380 → 841 ns.
Essa cauda variável, inferior a 0,4% do tempo GPU no caso final, não comprova
regressão relevante de preparação/submissão nem ganho no command processor.
MSAA1 usa exatamente o mesmo
bytecode e teve P50 dentro de 0,5%, com caudas variáveis. Os recursos finais
são byte a byte os mesmos das variantes medidas aceitas.

| Recurso | Baseline | Rejeitado | Novo |
|---|---:|---:|---:|
| DXBC tokens de instrução / temps declarados | 1362 / 24 | 1118 / 23 | 1251 / 24 |
| SPIR-V instruções | 2764 | 2403 | 2198 ambas as fontes |
| SPIR-V UDiv / UMod | 2 / 1 | 21 / 16 | 6 / 0 |
| Pipelines resolve, modo escalado D3D12/Vulkan | 19 / 19 | candidato separado | 20 / 20 |

Modo sem escala continua 19/19; a sonda cria um PSO ativo por caso/API.
Nenhum buffer intermediário, cópia, conversão ou memória workgroup nova;
buffers da sonda continuam **328 MiB + 16 bytes** nas três variantes.
Os dois novos blobs somam 80.772 bytes. Arrays de handles aumentam 8 bytes
por backend/cache. Memória privada e latência de criação de
PSOs no driver não foram medidas; contagens IR não são instruções SASS.

## Reprodução e limites

Evidências locais separadas em `.local/0202/` nesta worktree:
`red.*.log`, `shipping-green.*.log`, `focused-results.json`, `probes-build2.log`,
`replay.log`, `audit.json`, `final-instructions.json`, `performance-*.json`.
O patch contém 14 arquivos SDK: seleção/cache de ambas APIs, helper de endereço,
wrappers, dois recursos e manifests. No projeto: lock, gerador determinístico,
sondas CPU/GPU, verificador de recursos, benchmark de três variantes e este relatório.

Gerar/verificar com `scripts/shaders/generate_msaa_scaled_resolve.py --verify`
e os mesmos argumentos `--sdk-root`, `--fxc`, `--spirv-compiler`, `--spirv-tools`
usados nos geradores anteriores. Readback:
`x360n_gamma_resolve_gpu_checks --extended-fixed16 --extended-rgb565 --baseline-dir <original>`.
Negativo: `--extended-rgb565 --shader-dir <original> --baseline-dir <original>`.
Benchmark: `benchmark_gamma_resolve.py --rgb565 --trials 3 --reference <original>
--rejected <0f96a19> --candidate <0202> --json <local>`, repetir com `--source64`.

Readback é de compute real na RTX 4070; equivalência com o Canary e com o
oráculo não comprova hardware Xbox 360. Próxima etapa: cenas sintéticas do
pipeline rasterização → EDRAM → resolve → readback nas duas APIs.
Validação visual e gameplay pendentes; nenhum jogo executado, ganho de FPS
ou gameplay aprovado declarado.
