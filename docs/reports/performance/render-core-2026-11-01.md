# RENDER-CORE — captura, replay e perfil por frame do Native — 01/11/2026

**Infraestrutura implementada e testada em Linux/Vulkan (lavapipe); caminho
D3D12 compilado só por checagem de sintaxe, sem execução nesta sessão (nuvem,
sem Windows/GPU).** Nenhum efeito, resolução, física, timing ou semântica GPU
foi alterado. Sem hacks por título, sem renderer novo. Sem afirmação de FPS.

Base: `main` `012d6c0` (inclui PR #9, RENDER-FINAL e PERF-GPU-02 Windows).

## Por que

RENDER-FINAL mediu em Skate 3/Native D3D12 cerca de 2.800 draws, 25 resolves e
6 submissões por frame, fila GPU ocupada ~14,6 ms e ociosa ~42 ms por frame,
esperas de fence ~13,6 ms. Não há como saber, só com gameplay manual, se o
caminho crítico é o renderer, o command processor ou a CPU do título; e cada
otimização dependia de sessões manuais não determinísticas (tráfego, câmera).

## O que foi implementado

### Fronteira escolhida

`ScanoutBackend` (interface entre o command processor Native e o renderer) é o
ponto de gravação: draws, resolves, sincronizações guest, leituras de
pacotes, contadores de amostras, shaders e present passam por ela. Um
decorador (`src/native/capture/capture_backend.*`) grava e mede sem tocar no
código D3D12/Vulkan; desligado por padrão (os cvars abaixo vazios = o
decorador nem é instalado, custo zero).

### Captura (`native_capture`)

- Chamadas em ordem; registradores como diferenças; shaders pelo ucode
  original; argumentos completos (índices, propósito do draw, LUT, overlay,
  efeito pós).
- Memória guest lida por cada chamada: fetch de vértices dos bindings do VS,
  índices, texturas dos bindings VS/PS (base e mips), retângulo e páginas de
  destino dos resolves, scanout e overlay. Emitida só quando os bytes mudaram
  desde a última gravação daquela faixa; blobs deduplicados por conteúdo
  (XXH3-128). Só páginas alocadas pelo título são lidas.
- Estado persistente que não está na memória guest: **EDRAM inteira + cauda de
  contadores de amostras + estado de contagem** no início (`ReadEdram`).
- No present: hash do scanout e de toda faixa escrita por resolves/MEMEXPORT no
  frame, após a publicação — as expectativas do replay.
- Início por `native_capture_skip` presents ou por arquivo
  (`native_capture_trigger`: começa no primeiro present após o arquivo existir,
  para sessões manuais). Exige escala de resolução 1 (cache de resolves
  escalados não é capturado; recusado com erro explícito).
- Formato `X360NFC1` versionado, leitura rejeita arquivos truncados/inválidos.
  **Capturas contêm dados do título: somente em `.local/`.**

### Replay determinístico (`x360n_native_replay`)

Executável que liga as fontes do plugin (mesmo padrão dos perf probes), cria
runtime em modo ferramenta, provider e backend D3D12 ou Vulkan, e executa as
chamadas gravadas sem título, sem threads guest e sem PM4. Memória é aplicada
**só onde difere** da memória do replay, como escrita da CPU
(`NotifyPhysicalWrite`): bytes que a GPU do replay produziu e ainda não
publicou continuam sendo da GPU, como na captura. `--repeat N` repete a
captura (passes seguintes aquecidos); cada frame confere scanout, faixas
escritas pela GPU e cada leitura de contadores contra a captura. Saída:
`frames.csv` e `replay.json`; código 0 = equivalente, 3 = diferença.

Para isso os dois backends ganharam `CompleteFrame` (a parte guest-visível do
Present sem presenter: publicar, submeter, encerrar o frame), `ReadEdram`/
`WriteEdram` e `Counters` (submissões, esperas de fence, tempo de espera, GPU
ocupada quando `native_stats`/`native_vulkan_perf` estão ligados; campos não
medidos ficam vazios, nunca zero). O `Present` real chama o mesmo
`CompleteFrame` antes de apresentar: um único caminho.

### Perfil por frame (`native_frame_profile=ARQUIVO.csv`)

Uma linha por frame guest, ao vivo ou no replay, mesmas colunas: tempo do
frame; tempo **dentro** do renderer por tipo de chamada (draw, resolve, sync,
contadores, shaders, present) e contagens; `outside_ms` = thread do command
processor fora do renderer (decodificação PM4 e espera pelo título); contadores
do backend no frame (submissões, esperas e tempo de fence, GPU ocupada).
Interpretação do caminho crítico: `outside_ms` alto = renderer não é o gargalo
daquele frame; replay mais rápido que o frame ao vivo = idem; `fence_wait_ms`
próximo de `gpu_busy_ms` = CPU e GPU serializadas.

## Referências estudadas

- `arcanite24/shiftglue-sdk` (`a069c91`, BSD-3): dump/replay de frame do FH1 em
  nível PM4, páginas no primeiro toque (dados de GPU tirados do espelho),
  comparação do front buffer, `fh1_frame_replay_repeat` como benchmark
  determinístico. Aproveitado: repetição, comparação do scanout, captura por
  página. Diferente aqui: gravação na fronteira do renderer (o replay isola o
  Native, não o CP), memória por chamada (escritas da CPU no meio do frame) e
  snapshot de EDRAM (não gravado no dump do FH1). Executor nativo do FH1 é
  específico do título; não usado.
- `mchughalex/skate3recomp` (`f6e0ae8`): renderer nativo **específico de Skate
  3**, lê entidades, materiais e paletas do motor. Incompatível com a regra de
  suporte genérico; nenhuma técnica transplantada.
- Xenia Canary: referência de semântica (resolve/EDRAM/ZPD) já seguida pelo
  Native; o Canary não tem captura de frame neste nível.

## Otimização estrutural medida: Vulkan, uma espera de GPU a menos por leitura de contadores

O replay mostrou o custo concentrado em leituras de contadores. No Native
Vulkan, `ReadSampleCounts` publicava resolves pendentes (drenagem completa da
GPU) só para liberar os 32 primeiros bytes do buffer de readback de resolves,
e drenava de novo para a própria cópia. Agora os contadores têm buffer de
readback próprio: uma drenagem só, que também completa os resolves. O D3D12 já
usava buffer próprio; não havia o que mudar ali.

`x360n_native_frame_probe` (frame com draw rasterizado acumulando na EDRAM,
ZPD, resolve, draw com teste de visibilidade enquanto o resolve está pendente,
swap), captura de 5 frames, replay `--repeat 11 --stats`, 7 pares em ordem
alternada, passes aquecidos (50 frames por execução), lavapipe em 4 CPUs:

| Medida | `main` | candidato |
|---|---:|---:|
| Esperas de fence por frame | 5,00 (todas as execuções) | **4,00** (todas as execuções) |
| Frame aquecido, média (mediana dos 7) | 28,144 ms | 25,802 ms |
| Razão pareada do frame | — | 0,955 [0,771; 1,081] |
| Equivalência | 55 scanout, 55 faixas GPU, 220 contadores: 0 diferenças | idem |

A redução de uma drenagem completa é determinística; **o ganho de tempo não
está confirmado** (intervalo inclui 1; a "GPU" do lavapipe usa as mesmas CPUs).
Dados: [JSON](render-core-2026-11-01.json).

**Otimização estrutural do D3D12: não feita nesta entrega.** Escolhê-la exige
o perfil/replay de uma captura real (Skate 3) em GPU real; a infraestrutura
acima existe para isso. Nenhuma mudança D3D12 de desempenho foi feita sem
medição.

## Testes executados (Linux x64, lavapipe)

| Teste | Resultado |
|---|---|
| `native.capture-replay-vulkan`: 8 frames, captura dos frames 4–7, perfil ao vivo com 8 linhas, replay 3 passes = 12 scanouts/12 faixas/24 contadores iguais | aprovado |
| Controle negativo no mesmo teste: replay sem restaurar EDRAM deve divergir | detectado (4/4 frames, saída 3) |
| `native.capture-format` (ordem, deduplicação, present, arquivo truncado/inválido) | aprovado |
| Captura por arquivo-gatilho (início no present 321, 2 frames, replay equivalente) | aprovado (manual) |
| `native.pipeline-recording`, `native.pipeline-cold-vulkan`, `native.vulkan-components`, `sdkcheck.Native-MEMEXPORT-vulkan` (88/88), `sdkcheck.Native-FSI-sample-counts`, `sdkcheck.Native-FrameTiming` | aprovados |

Build completo Linux x64 Release aprovado. D3D12: arquivos alterados checados
com `-fsyntax-only` usando DirectX-Headers (MIT) e shim Win32 local fora do
repositório; restam apenas erros do shim (constantes D3DCompile, cabeçalho
gerado no Windows, `uint64_t` do Linux), nenhum no código novo.

Limites conhecidos: MEMEXPORT com endereço dinâmico não entra na verificação
(faixa = memória inteira); texturas são gravadas pelos bindings (conservador:
pode gravar mais que o lido); a decisão de visibilidade do CP é gravada, não
reexecutada; o replay não mede o PM4 nem a CPU do título.

## Como capturar e analisar Skate 3 no Windows (D3D12, GPU real)

Build (inclui o plugin, o replay e os testes novos):

```powershell
scripts\devshell.cmd cmake --build --preset win-amd64-release --target rexgpu-native x360n_native_replay x360n_native_frame_probe x360n_native_pipeline_tests
scripts\devshell.cmd ctest --preset win-amd64-release -R "native[.]capture|native[.]pipeline|Native-" --output-on-failure
```

Sessão Skate 3 com perfil ao vivo e captura por gatilho (mesmo preparo/seed do
RENDER-FINAL; acrescentar aos argumentos do cenário de `benchmark-runtime.ps1`
ou à linha de comando do executável):

```text
--gpu_plugin=native --gpu_backend=d3d12 --native_stats=true
--native_frame_profile=.local\captures\skate3\live-frames.csv
--native_capture=.local\captures\skate3\street.x360nfc
--native_capture_trigger=.local\captures\skate3\go
--native_capture_frames=3
```

Quando a cena da rua estiver na tela: criar o arquivo-gatilho
(`New-Item .local\captures\skate3\go`). O log registra "recording after
present N" e o tamanho final. A captura grava a EDRAM (espera a GPU uma vez)
e hashes do frame; o frame em que ela começa fica mais lento no CSV
(`outside_ms`).

Replay e perfil sem o jogo (repetível, sem tráfego/câmera variando):

```powershell
.local\build\win-amd64\src\native\Release\x360n_native_replay.exe .local\captures\skate3\street.x360nfc .local\captures\skate3\replay-d3d12 --backend d3d12 --repeat 30 --stats
```

`replay.json` deve ter `"equivalent":true`; `frames.csv` dá o custo do renderer
isolado. Comparar com `live-frames.csv`: se o frame do replay for muito menor
que o ao vivo, o gargalo está fora do renderer (CPU do título/CP,
`outside_ms`); se não, os campos `draw_ms`, `sync_ms`, `fence_wait_ms` e
`gpu_busy_ms` apontam a fase. A/B de uma mudança: o mesmo `.x360nfc` com o
replay da `main` e o do candidato, ordem alternada, e as duas execuções devem
ser equivalentes. `--backend vulkan` reproduz a mesma captura no Native Vulkan
(a captura independe da API; igualdade de pixels entre APIs não é garantida).
