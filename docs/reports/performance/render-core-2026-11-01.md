# RENDER-CORE — captura, replay e perfil por frame do Native — 01/11/2026

**Captura e replay determinístico implementados e executados em CI sem GPU:
Linux/Vulkan (Mesa lavapipe) e Windows/D3D12 (WARP), ver "Testes".** Nenhum
efeito, resolução, física, timing, AOT ou semântica GPU foi alterado. Sem hacks
por título, sem renderer novo. Sem afirmação de FPS: nenhuma medição em GPU real
nem com Skate 3 foi feita nesta nuvem (sem GPU e sem o jogo); os comandos para
o teste local estão no fim.

Base: `main` `012d6c0` (inclui PR #9, RENDER-FINAL e PERF-GPU-02 Windows).

## Por que

RENDER-FINAL mediu em Skate 3/Native D3D12 cerca de 2.800 draws, 25 resolves e
6 submissões por frame, fila GPU ocupada ~14,6 ms e ociosa ~42 ms por frame,
esperas de fence ~13,6 ms. Só com gameplay manual não há como separar renderer,
command processor e CPU do título, e cada otimização dependia de sessões não
determinísticas (tráfego, câmera).

## Fronteira e captura (`native_capture`)

`ScanoutBackend` (interface entre o command processor Native e o renderer) é o
ponto de gravação: draws, resolves, sincronizações guest, leituras de pacotes,
contadores de amostras, shaders e present passam por ela. Um decorador
(`src/native/capture/capture_backend.*`) grava e mede sem tocar no código
D3D12/Vulkan; com os cvars vazios ele nem é instalado (custo zero).

- Chamadas em ordem; registradores como diferenças; shaders pelo ucode
  original; argumentos completos (índices, propósito do draw, LUT, overlay,
  efeito pós).
- Memória guest lida por cada chamada (vértices dos bindings do VS, índices,
  texturas VS/PS com mips, destino dos resolves, scanout, overlay), emitida só
  quando os bytes mudaram; blobs deduplicados por conteúdo (XXH3-128).
- **Páginas produzidas pela GPU são resultado, não entrada.** A primeira versão
  desta entrega gravava os bytes que o resolve escreveu no frontbuffer como
  entrada do frame seguinte; o replay os restaurava e a conferência do scanout
  passava mesmo sem o renderer produzir nada (o controle sem EDRAM só falhava
  pelos contadores). Corrigido: faixas escritas por resolve (extensão exata) e
  MEMEXPORT ficam marcadas como da GPU, não são regravadas como entrada e são
  conferidas por hash no fim do frame, após a publicação. O workload de teste
  foi refeito para depender explicitamente da EDRAM.
- Estado persistente fora da memória guest: EDRAM inteira + cauda de contadores
  de amostras + estado de contagem no início.
- No present: hash do scanout, de cada faixa escrita pela GPU, da **saída do
  presenter** (imagem composta, com dimensões) e o aspecto de exibição.
- Início por `native_capture_skip` presents ou por arquivo-gatilho
  (`native_capture_trigger`); `native_capture_frames` frames. Exige escala de
  resolução 1 (recusa explícita).
- **Capturas contêm dados do título: somente em `.local/`, nunca no Git.**

### Formato `X360NFC1` versão 2 e validação

Cabeçalho de 80 bytes; registros `{u32 tipo, u64 tamanho, payload}`. Antes de
qualquer chamada ao renderer, `capture::ValidateFile` percorre o arquivo
inteiro e cada registro passa por um decodificador que confere, antes de todo
acesso ou `memcpy`: tamanho exato do payload, tipo conhecido, blob referenciado
existente e com o tamanho declarado, faixas dentro dos 512 MiB físicos sem
overflow, índices de registradores `< kRegisterCount`, shader com id/tipo/
tamanho válidos e declarado antes do draw, enumerações (primitiva, propósito,
formato/endian de índices, origem de sync, efeito pós), LUT com 256 ou 384
entradas conforme o formato, overlay coerente, dimensões ≤ 8192/16384 e pares
largura/altura e aspecto consistentes, número de frames igual ao cabeçalho.
Versão 1, magia errada, escala fora de 1–4, nome de backend sem terminador,
truncamento em qualquer byte e corrupção são recusados com código 2.

## Replay determinístico (`x360n_native_replay`)

Executável com as fontes do plugin, runtime em modo ferramenta, backend D3D12
ou Vulkan, sem título, sem threads guest e sem PM4. Memória aplicada **só onde
difere** da memória do replay, como escrita da CPU (`NotifyPhysicalWrite`).
Present real por um presenter offscreen (`CreatePresenter` +
`CaptureGuestOutput`): a mesma composição do jogo (LUT gamma, overlay, FXAA/
efeito pós, presenter) é executada e a imagem final é comparada por hash e
dimensões com a captura — equivalência visual além de memória/EDRAM.

Por frame e por passe: scanout, faixas escritas pela GPU, toda leitura de
contadores e saída do presenter. `--repeat N`: cada passe gera um digest de
todos os resultados; passes diferentes entre si = **não repetível**. Saída
`frames.csv` + `replay.json` (`equivalent`, `repeatable`, checagens,
diferenças, `per_frame`). Códigos: 0 = equivalente e repetível, 2 = arquivo
inválido, 3 = diferença ou não repetível.

Controles negativos (só diagnóstico): `--skip-edram` (não restaura a EDRAM) e
`--tamper-memory N` (altera 16 bytes do N-ésimo registro de memória).

`per_frame` (com `--stats`): médias dos passes aquecidos de frame, renderer e
cada fase (draw, resolve, sync, contadores, present, shaders), draws, resolves,
syncs, submissões, esperas e tempo de fence, GPU ocupada/ociosa e o gargalo
dominante (`draw_recording`, `resolves`, `guest_syncs_and_counters`,
`present`, `shader_loading`, `gpu`, `unmeasured`).

## Perfil por frame (`native_frame_profile=ARQUIVO.csv`)

Uma linha por frame, ao vivo ou no replay:
`frame,frame_ms,backend_ms,outside_ms,capture_ms,profile_ms,draws,…`.
`capture_ms` = custo da gravação (leitura/hash de memória, EDRAM, escrita) e
`profile_ms` = custo de escrever o CSV, medidos à parte;
`outside_ms = frame − backend − capture − profile`, isto é, só o tempo do
command processor fora do renderer (PM4 e espera pelo título). Antes a captura
era atribuída ao jogo em `outside_ms`.

## Otimizações estruturais

### Vulkan: uma espera de GPU a menos por leitura de contadores

`ReadSampleCounts` drenava a GPU para liberar o readback de resolves e drenava
de novo para a própria cópia; agora tem readback próprio (5 → 4 esperas por
frame, determinístico; tempo sem ganho confirmado: razão 0,955 [0,771; 1,081]).
Dados: [JSON](render-core-2026-11-01.json).

### D3D12 e Vulkan: submissão antecipada com a GPU ociosa (`native_eager_submit`)

Gargalo comprovado pelo replay: no workload pesado (`--draws 1024 --resolves
8`), `per_frame.bottleneck = guest_syncs_and_counters`, GPU ociosa ~37 ms do
frame enquanto a CPU gravava trabalho que só era submetido na próxima
sincronização — o mesmo padrão do Skate 3 no RENDER-FINAL (GPU ociosa 42 ms).
Mudança: quando há `native_eager_submit` draws gravados (um resolve conta como
o limiar inteiro) e a última submissão já terminou, a lista é submetida. Ordem
e conteúdo do trabalho GPU não mudam; no D3D12 a causa aparece como `eager=` no
log de `native_stats`.

Equivalência: `native.capture-replay-*` grava o workload pesado e o reproduz
com `native_eager_submit=0` e `=1` (o mais agressivo): os dois equivalentes e
repetíveis em 3 passes, e o braço `1` precisa submeter mais (lavapipe: 7 → 8
submissões por frame).

A/B no mesmo `.x360nfc` (`scripts/performance/native_replay_ab.py`, ordem
alternada, todas as execuções equivalentes), lavapipe, 4 CPUs, `0` vs `64`:

| Workload | Pares | GPU ociosa | Fence (ms) | Frame aquecido | Razão pareada |
|---|---:|---|---|---|---|
| 1024 draws + 8 resolves | 7 | 37,6 → 23,5 ms | 53,8 → 52,9 | 90,8 → 89,0 ms | 0,98 [0,88; 1,14] |
| 256 draws + 4 resolves | 9 | 12,8 → 10,5 ms | 38,0 → 43,9 | 49,3 → 56,5 ms | 1,06 [1,02; 1,21] |

A ociosidade da GPU cai como previsto, mas no lavapipe a "GPU" usa as mesmas
CPUs que gravam os comandos e o frame não encurta (piora no workload menor).
**Por isso `native_eager_submit` fica 0 (desligado) por padrão**; 64 é o valor
candidato para o A/B em GPU real com capturas do Skate 3 (comando abaixo). Sem
afirmação de ganho. Dados: [JSON](render-core-eager-submit-2026-11-01.json).

## Testes

| Teste | Plataforma | Resultado |
|---|---|---|
| `native.capture-format`: 6 casos / 75 asserções — ordem, deduplicação, present, payloads malformados por decodificador, inconsistências do present, estrutura do arquivo, todo truncamento e byte corrompido; ASan/UBSan limpos | Linux | aprovado |
| `native.capture-replay-vulkan`: efeitos `none` e `fxaa`, 8 frames, captura 4, replay 3 passes = 12/12 scanouts, 12/12 faixas GPU, 48/48 contadores, 12/12 saídas do presenter, repetível | Linux lavapipe | aprovado |
| Mesmo teste, controles: sem EDRAM → 4/4 frames diferentes (3); 16 bytes de vértices alterados → frame diferente (3); arquivo truncado → recusado (2) | Linux lavapipe | detectados |
| Mesmo teste, workload pesado com `native_eager_submit` 0 e 1 | Linux lavapipe | equivalentes, repetíveis |
| `native.pipeline-recording`, `native.pipeline-cold-vulkan`, `native.vulkan-components`, `sdkcheck.Native-MEMEXPORT-vulkan`, `sdkcheck.Native-FSI-sample-counts`, `sdkcheck.Native-FrameTiming` | Linux lavapipe | aprovados |
| `native.capture-replay-d3d12`, `native.capture-format`, `native.pipeline-recording` (workflow `native-tests.yml`) | Windows WARP | ver PR |

O job Windows compila de verdade o plugin D3D12, o replay e os testes com o
toolchain fixado (não é checagem de sintaxe) e executa os mesmos contratos com
`d3d12_adapter=-2` (WARP). Os dois jobs também rodam um A/B informativo de
`native_eager_submit` 0/64 sobre a captura pesada e publicam o JSON.

Limites conhecidos: MEMEXPORT com endereço dinâmico não entra na verificação;
texturas são gravadas pelos bindings (conservador); a decisão de visibilidade
do CP é gravada, não reexecutada; o replay não mede o PM4 nem a CPU do título;
WARP e lavapipe não representam o desempenho de GPU real.

## Skate 3 no Windows (D3D12, GPU real) — teste local

Build (o `|` não pode ir num argumento de `devshell.cmd`: um `-R` por chamada):

```powershell
scripts\devshell.cmd cmake --build --preset win-amd64-release --target rexgpu-native x360n_native_replay x360n_native_frame_probe x360n_native_pipeline_tests
scripts\devshell.cmd ctest --preset win-amd64-release -R "native[.]capture" --output-on-failure
```

Sessão com perfil ao vivo e captura de **3 frames** por gatilho (mesmo preparo/
seed do RENDER-FINAL; acrescentar aos argumentos do cenário de
`benchmark-runtime.ps1` ou à linha de comando do executável):

```text
--gpu_plugin=native --gpu_backend=d3d12 --native_stats=true
--native_frame_profile=.local\captures\skate3\live-frames.csv
--native_capture=.local\captures\skate3\street.x360nfc
--native_capture_trigger=.local\captures\skate3\go
--native_capture_frames=3
```

Com a cena da rua na tela: `New-Item .local\captures\skate3\go`. O log registra
"recording after present N" e o tamanho final; `capture_ms` mostra o custo da
gravação, separado de `outside_ms`.

Replay repetido, sem o jogo:

```powershell
.local\build\win-amd64\src\native\Release\x360n_native_replay.exe .local\captures\skate3\street.x360nfc .local\captures\skate3\replay-d3d12 --backend d3d12 --repeat 30 --stats native_stats=true
```

`replay.json`: `"equivalent":true`, `"repeatable":true` e `per_frame` com
draws, resolves, submissões, esperas de fence, GPU ocupada/ociosa e
`bottleneck`. Replay muito mais rápido que `live-frames.csv` = gargalo fora do
renderer (`outside_ms`).

A/B da submissão antecipada nos mesmos frames (mesmo build, um braço com a
mudança desligada; para dois builds, `--baseline`/`--candidate` diferentes):

```powershell
python scripts\performance\native_replay_ab.py --baseline .local\build\win-amd64\src\native\Release --candidate .local\build\win-amd64\src\native\Release --capture .local\captures\skate3\street.x360nfc --output .local\captures\skate3\ab-eager-submit.json --backend d3d12 --rounds 7 --repeat 10 --cvar native_stats=true --baseline-cvar native_eager_submit=0 --candidate-cvar native_eager_submit=64
```

O script aborta (código 3) se qualquer execução divergir da captura. Ligar
`native_eager_submit` por padrão só se a razão pareada do frame ficar abaixo
de 1 com intervalo que não inclua 1, e o A/A do mesmo build mostrar o ruído.
