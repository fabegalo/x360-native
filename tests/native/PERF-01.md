# Native N-PERF-01 — 2026-10-25

Base `5b87e8c`, RTX 4070 / driver 617.42, LLVM 22.1.8, Release. SDK pinado e 256 arquivos do lock conferidos em snapshot isolado; DLLs/libs reutilizadas por hash. Sem alteração do checkout/SDK do trabalho 0202. Build do plugin Native D3D12/Vulkan e do target `x360n_native_perf_probe` aprovado.

`Flush(false)` drenava toda submissão com writebacks. Agora cada slot mantém staging, offsets, tickets escalados e gerações CPU até o serial completar. Publicação em ordem; leituras dependentes esperam pelo último serial sobreposto. Eventos, contadores, reuso e destruição continuam sincronizados. Sem polling extra quando não há writebacks.

Sete pares alternados por cenário, 64 operações de aquecimento + 4.096 medidas; sem builds/testes GPU concorrentes. Caches do driver preservados, pipelines aquecidos; não foi medido cache frio. Tempos abaixo são medianas de lotes inteiros, incluindo a conclusão final. Ganho usa razões pareadas; IC bootstrap exploratório de sete pares.

| Cenário | Lote antes → depois (ms) | Redução pareada | Waits antes → depois | Fence antes → depois (ms) |
|---|---:|---:|---:|---:|
| export 1× | 440.23 → 164.89 | 62.84% | 4096 → 139 | 236.40 → 7.96 |
| resolve 1× | 408.23 → 197.66 | 50.84% | 4096 → 222 | 225.77 → 12.63 |
| sync 1× | 433.82 → 438.71 | -0.32% | 4096 → 4091 | 233.34 → 227.76 |
| none 1× | 255.85 → 252.34 | 2.87% | 4094 → 4094 | 27.99 → 32.97 |
| resolve 2× | 5119.61 → 4223.57 | 18.41% | 4096 → 1 | 1127.56 → 0.33 |
| resolve 4× | 7548.34 → 6308.26 | 16.14% | 4096 → 1 | 1409.63 → 0.13 |
| export 1× OFF | 435.93 → 194.08 | 56.23% | instrumentação OFF | indisponível |
| resolve 1× OFF | 373.61 → 230.44 | 38.54% | instrumentação OFF | indisponível |
| sync 1× OFF | 408.59 → 411.45 | -1.55% | instrumentação OFF | indisponível |
| none 1× OFF | 224.87 → 227.86 | -0.95% | instrumentação OFF | indisponível |

CPU, GPU, P50/P95/P99, 1% low sintético, RAM/VRAM, readbacks, bytes, alocações, hashes e intervalos estão em `perf_results.json`; logs e saídas completas em `.local/perf/`. Latências são de operações sintéticas, não frames guest. GPU: intervalos de timestamps por submissão, não ocupação exclusiva. VRAM: amostra DXGI depois do lote; RAM: pico do processo/amostras de private bytes. Não são picos de residência VRAM.

O staging limitado a três slots acrescentou aproximadamente 32 MiB de RAM; VRAM local reportada ficou equivalente em 1×. Não há alocações de buffers novas no trecho aquecido 1×. Scaled ainda aloca dois buffers por resolve e é dominado por preparação/publicação CPU; isso não foi alterado. O contador de bytes canônicos não inclui a cópia auxiliar escalada: em 2×/4× neste fixture, total GPU→CPU = bytes canônicos × (1 + escala²).

Validação nova: 542 asserções/10 casos CPU; três cenários GPU em 1×/2×/4× com fila bloqueada por evento, três slots em voo, leitura distinta sem espera, CPU sobrescrevendo antes da conclusão, revalidação por geração, sobreposição ordenada, contadores e eventos. Staging de 17 MiB, padding e offsets comparados byte a byte. Resolves de cores conhecidas, snapshots/scanout escalados e invalidação CPU aprovados. Destruição e três recriações encerraram sem buffers Native vivos. Todas as saídas GPU/CPU dos pares foram idênticas (1 MiB/par).

Controles existentes **não estão verdes**: MEMEXPORT D3D12 falha em “VS-only incomplete triangle” e “PM4 updates resident alias”; Vulkan tem essas duas mais quatro casos “signed integer packing”. Oclusão nas duas APIs para em “ZPD physical publication did not invalidate guest aliases”. Cada saída funcional foi reproduzida integralmente e idêntica no plugin baseline reconstruído; não são apresentadas como aprovações nem corrigidas fora deste escopo. WAIT_REG_MEM/comandos de memória: roteamento coberto pelos testes CPU focados, sem certificar contratos end-to-end além desses probes.

Instrumentação desligada por padrão (`native_vulkan_perf=false`), habilitar antes da criação do backend. O probe expõe os contadores do caminho real sem aumentar a ABI pública do plugin. Exemplo após build isolado:

```text
scripts/devshell.cmd cmake --build <build> --config Release --target rexgpu-native x360n_native_perf_probe
x360n_native_perf_probe.exe <logs> export 4096 1 1 contracts
x360n_native_perf_probe.exe <logs> resolve 4096 4 1 contracts
python tests/native/perf_pairs.py --baseline <antes.exe> --candidate <depois.exe> --output <pares> --rounds 7
```

Pendentes: validation layers com synchronization validation, falhas preexistentes dos probes, gameplay e fidelidade visual. Nenhum jogo, Computer Use, suíte completa, push ou integração foi executado. Ganho sintético não certifica FPS de jogos.
