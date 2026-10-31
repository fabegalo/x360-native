# PERF-GPU-02 — continuação Windows sobre o PR #9

## Base e escopo

O trabalho parte do PR #9 (`3df30fc45fdf6490216408804851657d76d12c5a`),
integrando a main `58cd9dd` no merge local `85ccbdf`. As alterações anteriores
foram guardadas em `.local/perf-gpu-02/pre-pr9-backup/` antes da troca de base.
O pipeline AOT genérico não foi modificado; seus patches 0237–0244 permanecem.

Implementado sobre os workers e a gravação diferida do PR:

- Native D3D12/Vulkan: criação assíncrona dos PSOs computacionais de texturas,
  resolve, clear e downscale. Os jobs possuem o bytecode temporário; D3D12 retém
  a root signature e Vulkan cria/destrói o shader module dentro do job. Com zero
  workers, a criação continua síncrona. Falhas continuam propagadas no bind.
- Native D3D12: não submeter uma lista vazia depois de um resolve que já
  sincronizou o frame. Drain e dependências guest continuam esperando a fence
  anterior e publicando seus readbacks. O tracking cobre comandos imediatos e
  diferidos; timestamps diagnósticos sozinhos não forçam uma submissão.
- Native Vulkan: reutilizar o descriptor set imutável de memória guest/EDRAM
  dentro da arena existente. O pool só é reciclado após sua fence.
- Native D3D12/Vulkan: acertos de textura residente leem atomicamente o bit
  de invalidação, evitando adquirir a região crítica global a cada binding.
  Todos os acessos concorrentes ao bit são atômicos; instalação/remoção dos
  watches e seus handles continuam protegidos pelo lock original. Recursos
  continuam pertencendo à thread de comandos e às arenas que os retêm.
- Native D3D12/Vulkan: `WAIT_REG_MEM` sobre registradores não força publicação
  de memória guest. A leitura continua no decoder original; dependências GPU
  continuam ordenadas pelos backends. Polls de memória preservam a publicação,
  `PrepareForWait` continua drenando quando há espera efetiva e `WAIT_FOR_IDLE`
  mantém a exigência de GPU ociosa. A mudança foi motivada pelos milhares de
  drenos `3C` observados em Skate 3; não há condição por título.
- Native Vulkan: aguardar as criações pendentes antes de mesclar um cache
  carregado após o primeiro resolve. Criações comuns continuam concorrentes;
  não há mutex novo no caminho dos draws. O requisito de exclusão na mesclagem
  consta em [vkMergePipelineCaches](https://docs.vulkan.org/refpages/latest/refpages/source/vkMergePipelineCaches.html).
- Xenos Vulkan, patch 0245: compartilhar um `VkPipelineCache` de driver entre
  os workers existentes, destruído após seu encerramento. Não substitui o
  cache guest nem acrescenta outro pool de compilação. A criação sem a flag
  `EXTERNALLY_SYNCHRONIZED` permite os acessos concorrentes de criação previstos
  em [vkCreateGraphicsPipelines](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html).
- Propagar `REXGLUE_ENABLE_PERF_COUNTERS` ao plugin Native quando habilitado;
  sem isso a coleta de frames ficava vazia no consumidor do SDK exportado.
  A mesma correção de instrumentação está na base e na candidata medidas.

Não há alteração de física, áudio, relógio guest, efeitos, MSAA ou resolução
para obter os resultados abaixo. Xenos D3D12 não recebeu mudança de execução.

## Gameplay real — resultados preliminares, sem aceite de FPS

Castle Crashers trial, perfil offline isolado criado pelas APIs existentes,
mesmos executável/runtime e seed em cada lançamento, Windows x64 Release,
LLVM 22.1.8, RTX 4070, 16 processadores lógicos. GPU identificada pelos logs;
as consultas CIM de CPU/RAM/driver não estavam disponíveis no sandbox.
Sem compilação concorrente nas janelas de medição. Cache da aplicação começa
com o mesmo seed; cache de driver/OS não foi limpo nem controlado.

O mantenedor controla o jogo. Navegação manual e progresso da cena diferem
entre as execuções: estes pares **não isolam causalmente o ganho**. Coleta de
30 s entre 100 e 130 s após o início do processo, contando swaps guest
concluídos, não scanout físico. Sessões encerradas pelo limite de 135 s.
1% low usa a média dos intervalos do 1% mais lento. FPS não é medição de GPU.

| Native | FPS | 1% low | P95 ms | P99 ms | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| D3D12 base PR | 59,93 | 44,81 | 19,09 | 20,47 | 13,52 |
| D3D12 compute assíncrono | 59,87 | 43,10 | 18,93 | 20,17 | 12,60 |
| D3D12 compute + listas vazias | 59,87 | 43,31 | 18,67 | 19,71 | 10,73 |
| Vulkan base PR | 59,77 | 39,01 | 18,59 | 19,88 | 11,35 |
| Vulkan compute + descritores | 59,64 | 33,75 | 19,56 | 21,42 | 10,73 |

**Nenhuma dessas rodadas demonstra aumento de FPS.** O D3D12 reduziu o
trabalho submetido de aproximadamente dois envios por frame para um, mantendo
as sincronizações guest. A CPU agregada por frame caiu 20,6% nesta comparação;
isso ainda inclui diferenças de cena e instrumentação. `native_stats=true`
também produz menos linhas de log quando existem menos submissões. Não tratar
essa porcentagem como ganho isolado em configuração normal.

Repetição D3D12 com `native_stats=false` nos dois lados (CSV de swaps ainda
ligado), combate observado junto aos depósitos de feno em chamas:

| Native D3D12 sem stats detalhadas | FPS | 1% low | P95 ms | P99 ms | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| Base PR | 59,90 | 43,89 | 19,02 | 20,26 | 11,56 |
| Candidata v3 | 59,84 | 42,00 | 18,53 | 19,98 | 10,94 |

CPU agregada -5,4%, P95 -2,6%, P99 -1,4%, porém 1% low -4,3%. O par
reproduz menor uso de CPU sem os timestamps/logs detalhados; não reproduz
ganho de FPS nem melhora uniforme do pacing. Ações manuais ainda diferem:
não é uma estimativa causal isolada, nem instrumentação completamente OFF.
Evidências `pr9-base-native-d12-no-stats` / `pr9-v3-native-d12-no-stats`.
Esse par antecede a correção de consultas a registradores (v4).

No Vulkan houve CPU menor, mas piora nos percentis/1% low desta rodada.
Registrar como **sem ganho demonstrado de frametime**, sem apagar o resultado
desfavorável. A correção de exclusão da mesclagem é posterior a essa rodada;
o lançamento normal inicializa o cache antes dos draws.

Xenos Vulkan, base recompilada sem 0245 contra candidata com 0245:

| Xenos Vulkan | FPS | 1% low | P95 ms | P99 ms | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| Base | 60,00 | 49,57 | 18,50 | 19,89 | 17,17 |
| Cache de driver compartilhado | 60,00 | 49,53 | 18,51 | 19,98 | 16,83 |

**Sem ganho demonstrado de FPS, caudas ou startup**: primeiro swap em
4.024 → 4.100 ms (não equivale à duração de tela preta). CPU -2,0% nesta
rodada não isola o efeito do cache. Os contadores de criação/tradução no CSV
vieram zerados apesar das criações registradas no log; não permitem quantificar
compilações evitadas. O probe confirma o uso do cache, não ganho de tempo.
O usuário abriu/fechou o Guide na candidata antes da janela medida.
Evidências: `pr9-base-xenos-vk-1` e `pr9-v3-xenos-vk-1` sob o mesmo diretório
de benchmarks listado abaixo.

Evidências completas, com argumentos, hashes de cada DLL, CSV e logs:

- `.local/evidence/benchmarks/pr9-base-native-d12-profile2/castle_crashers/`
- `.local/evidence/benchmarks/pr9-compute-native-d12-1/castle_crashers/`
- `.local/evidence/benchmarks/pr9-v2-native-d12-1/castle_crashers/`
- `.local/evidence/benchmarks/pr9-base-native-vk-1/castle_crashers/`
- `.local/evidence/benchmarks/pr9-v2-native-vk-1/castle_crashers/`
- `.local/perf-gpu-pr9/gameplay-summary.json` e `observations.json`.

O runner grava `scene_verified=false` por padrão; as observações manuais são
separadas. D3D12 foi observado em gameplay com HUD, inimigos, fogo e partículas.
A captura Computer Use do Vulkan retornou preto; o mantenedor confirmou ver
o jogo normalmente. Isso não prova comparação visual pixel a pixel.

## Skate 3 — diagnóstico de uma carga mais pesada

Reutilizado o executável preparado pelo incremento AOT 0244, com hashes
conferidos; nenhum codegen ou mudança de AOT. Perfil/saves de teste copiados
para fixture isolado, excluindo o conteúdo instalado global e caches. Sessões
de 175 s, janela de 140 a 170 s; usuário controla o jogo. Gameplay de rua e
Daly Estates observado, mas trajetos diferentes, sem pareamento de câmera.

| Native D3D12 Skate | FPS | 1% low | P95 ms | P99 ms | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| Base PR | 14,58 | 9,93 | 84,52 | 99,82 | 235,84 |
| v4, inclui consultas a registradores | 15,08 | 8,13 | 100,00 | 115,82 | 214,25 |
| v5, inclui leitura atômica da invalidação | 16,31 | 9,68 | 83,68 | 99,20 | 216,92 |

FPS +3,4% e CPU/frame -9,2% nesta rodada, porém caudas piores: **não constitui
aceite de fluidez**. Os custos internos na janela, alinhados pelo log, mostram
24,98 → 24,96 resolves/frame e 25,98 → 6,16 submissões/frame. A candidata teve
2.796 draws/frame contra 2.429 na base (+15,1%); custo do caminho de draw
12,63 → 12,16 µs/draw. Redução de submissões demonstrada em gameplay real,
com carga de geometria diferente explicitamente registrada.

Na v5 houve +11,8% de FPS e -8,0% de CPU/frame contra a base; P95 -1,0%,
P99 -0,6%, mas 1% low -2,6%. São resultados observados de gameplay, sem
isolar o efeito de cada mudança dos trajetos manuais e do estado de caches.
Carga registrada: 2.416 draws/frame, 25,03 resolves/frame e 6,10 submissões/
frame. Espera de fence por frame: 17,56 → 12,30 ms. O tempo do caminho de
draw por chamada **não melhorou** (12,63 → 13,01 µs), apesar de menor espera
de submissão. Não apresentar a leitura atômica isoladamente como ganho provado.
Dados normalizados e script em `.local/perf-gpu-pr9/skate-costs.json` e
`summarize-costs.ps1`; janela das amostras internas cobre aproximadamente 29,6 s.

Evidências `pr9-base-skate-native-d12/skate_3` e
`pr9-v4-skate-native-d12/skate_3` e `pr9-v5-skate-native-d12/skate_3`, com CSV,
argumentos, hashes e logs. Todas
registram 32 erros de VFS ligados a `big:`/`dlcbig:` ausentes; não são falhas
de GPU resolvidas por este incremento. As tabelas de Castle acima antecedem v4.

| Native Vulkan Skate | FPS | 1% low | P95 ms | P99 ms | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| Base PR | 13,25 | 3,85 | 97,34 | 102,77 | 248,23 |
| v5 | 13,76 | 9,58 | 98,37 | 101,40 | 238,67 |

FPS +3,8%, CPU/frame -3,9%, P99 -1,3%, mas P95 +1,1%. O 1% low da base
contém pausas mais longas; a melhora desta única janela não demonstra que
todos os stalls frios foram eliminados. O mantenedor confirmou cenário e
personagem normais na tela durante a base Vulkan. Nenhum controle foi enviado
pelo assistente. Evidências `pr9-base-skate-native-vk/skate_3` e
`pr9-v5-skate-native-vk/skate_3`. Mesmos limites de navegação manual, caches e
contadores gráficos zerados; não é certificação visual frente ao Canary.

As rodadas finais continuam em aproximadamente 14–16 FPS neste jogo:
**a lentidão geral Native não está resolvida**. Nenhuma configuração de
física, áudio, clock, resolução ou efeitos foi reduzida para melhorar os números.

## Correção e testes focados

Build Windows dos dois backends e dos probes com o SDK exportado, em harness
isolado `.local/perf-gpu-pr9/harness/`, sem recompilar o AOT dos títulos.

- Submissões vazias: teste base falha com `empty present/drain submitted GPU
  work`; candidata passa, incluindo submissão real e leitura de sample counts.
- D3D12 e Vulkan: resolves coloridos e snapshots ampliados 2x conferidos byte
  a byte; workers=2 e controle síncrono workers=0/1x; instrumentação D3D12 OFF
  no controle síncrono. Passaram gates de GPU em voo, reutilização das arenas,
  epochs de CPU, publicação sobreposta, eventos, counters e staging de 17 MiB.
- Vulkan: teste segura a criação compute real em um worker e exige que carregar
  o cache espere sua conclusão; depois drena a GPU. Aprovado.
- Vulkan: 48 pipelines/draws com exports conferidos, aprovado após reutilização
  dos descritores. D3D12: MEMEXPORT com debug layer aprovado após listas vazias.
- Patch 0245: probe de draw guest real exige cache de driver, mantendo o
  controle positivo/negativo de vertex fetch predicado. Base recompilada sem
  o patch falha; candidata passa. `apply-sdk-patches.ps1 -Check` aprova os
  patches do lock, incluindo 0245, sem mudanças manuais no SDK.
- Consultas a registradores: base falha no novo controle; candidata passa
  90 asserções/7 casos de pacotes. Probes MEMEXPORT reais D3D12/Vulkan passam
  após inserir um poll de registrador entre export e consumo por vertex fetch,
  seguido de poll de memória que exige a publicação do resultado da GPU.
  Logs `register-poll-{red,green}` e `register-poll-memexport-{d3d12,vulkan}`.

A primeira DLL Xenos do SDK compartilhado já continha o cache de driver e
passou indevidamente no ensaio chamado `driver-cache-red`. Esse resultado
**não é um controle negativo**. Uma base separada foi recompilada com os dois
arquivos originais cujos SHA-256 correspondem a `base_files` de 0245;
`driver-cache-red-exact.log` registra a falha correta. As medições Native
não executam esse plugin Xenos.

Os plugins congelados v3 estão inventariados em
`.local/perf-gpu-pr9/final-plugin-hashes.json`. Native base/v3:
`9686de84866e93d9bf5e1d64165b6e5debb1f6513585832b56d64a98b26e8823` /
`fd7fcb47f3f261a834dc85dd67d00ccc4102051f2da55b0d55fa6e5ba0506fcf`.
Xenos base/candidata:
`dda425999213e37b40bb65aa8a213fd4da35e8647b2d5cd62a66ca456e9399dd` /
`aebf742db27a272d998a2d5f7141cdd4e99fbdbdd3c36878fd6df52bc934a327`.
Cada `environment.json` identifica os artefatos exatos de sua rodada, inclusive
a candidata Native anterior à guarda de mesclagem tardia do cache.
Native v4: `dca1d9dc40727e2ff7b3431034bbb5c594740c7ed894d808670337b52e7c7dcf`.
Native v5: `cf270d516c2ee2aaa6025ff97bac5bd5d1bb228af52787c43a9bd20e234366fd`.
Após a mudança do bit de invalidação, os contratos reais dos dois backends
passaram novamente (resolves, snapshots 2x, epochs e publicações), logs
`resident-texture-contracts-{d3d12,vulkan}`. Não foi executado ThreadSanitizer.

Logs de testes e seus códigos de saída estão em `.local/perf-gpu-pr9/`:
`empty-submit-red`, `d12-contracts-{scale2,inline}`, `vk-contracts-{scale2,inline}`,
`vk-late-cache`, `v2-pipeline-vulkan`, `v2-memexport-d3d12`,
`driver-cache-red-exact` e `driver-cache-green` (`.log` e `.log.json`).

## Dashboard, Guide e limites

Sessão `pr9-v3-dashboard-functional/dash_17559`: Home original 17559 com perfil
PerfGPU, navegação, abertura/fechamento de HUD/Guide e gamerprofile, com retorno
às abas da dashboard. Observações da janela e logs dos módulos originais.
Instrumentação OFF, compilação concorrente: **não é benchmark**. Há erros e
stubs preexistentes de XLIVEBASE e caminhos ausentes no fixture; não se declara
aceite completo da dashboard nem de C7/C8 com base nessa sessão.

Sem validação de jogos além de Castle/Skate, Linux/macOS, ciclo completo de biblioteca/saves,
comparação controlada com Canary ou prova de superioridade visual. Não houve
retirada de efeitos nem alteração de timing como atalho para FPS.
