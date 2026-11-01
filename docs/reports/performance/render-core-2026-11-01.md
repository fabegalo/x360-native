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

### Formato `X360NFC1` versão 3 e validação

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
Versões 1 e 2, magia errada, escala fora de 1–4, nome de backend sem
terminador, truncamento em qualquer byte e corrupção são recusados com código 2.

**Imagem composta obrigatória (versão 3).** Antes, se `CaptureGuestOutput()`
falhasse, o present era gravado sem saída e o replay pulava a checagem visual
em silêncio. Agora: a captura é interrompida e o arquivo apagado quando o
presenter não devolve imagem completa (erro "Native frame capture rejected" no
log); o writer recusa present sem saída; o decodificador exige largura/altura
não nulas; o hash da imagem (`HashRgbxImage`) recusa imagem vazia, truncada,
com stride menor que a linha ou acima de 16384 — nunca compara menos linhas —
e cobre só RGB (o byte X é indefinido).

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

No replay, um presenter que não devolve imagem (ou devolve imagem inválida)
conta como diferença ("presenter returned no composed image"), e
`equivalent` exige uma checagem visual por frame (`visual_checked`).
`--no-present` não confere a imagem: `replay.json` diz
`"visual_checked":false` e o A/B recusa essa execução.

Tempo de frame no replay: do fim das checagens do frame anterior até o fim do
present — alimentação das chamadas gravadas mais o renderer. As checagens do
próprio replay (hashes, leitura da imagem do presenter) ficam fora.

Controles negativos (só diagnóstico): `--skip-edram` (não restaura a EDRAM),
`--tamper-memory N` (altera 16 bytes do N-ésimo registro de memória) e
`--drop-output` (simula presenter sem imagem).

`replay.json` traz P50/P95/P99 (posto mais próximo) do frame aquecido.
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

### D3D12 e Vulkan: submissão antecipada (`native_eager_submit`, `native_eager_submit_depth`)

Gargalo comprovado pelo replay: no workload pesado (`--draws 1024 --resolves
8`), `per_frame.bottleneck = guest_syncs_and_counters`, GPU ociosa ~37 ms do
frame enquanto a CPU gravava trabalho que só era submetido na próxima
sincronização — o mesmo padrão do Skate 3 no RENDER-FINAL (RTX 4070: fila GPU
ocupada 12,6–14,8 ms, esperas de fence 11,8–13,9 ms, ociosa 42 ms; CPU e GPU
serializadas).

Duas estratégias, as duas sem mudar ordem, barreiras nem sincronizações
visíveis ao guest (só acrescentam fronteiras de submissão):

- **ociosa** (`native_eager_submit=N`, `depth=0`): após N draws gravados (um
  resolve conta como N), submete se a fila já terminou tudo;
- **enfileirada** (`depth=1`): submete também com uma submissão ainda em
  execução, para a GPU receber o próximo lote antes de esvaziar. Com três
  arenas de gravação, a arena reutilizada é sempre a de uma submissão mais
  antiga: a CPU nunca bloqueia nisso (valores maiores são limitados a 1).

Equivalência: `native.capture-replay-*` grava o workload pesado e o reproduz
desligado, ocioso em 1 e enfileirado em 1: todos equivalentes e repetíveis, e
cada braço submete mais que o anterior (lavapipe: 7 → 8 → 9 por frame).

A/B no mesmo `.x360nfc` (`native_replay_ab.py`, 6 rodadas em ordem rotativa ×
4 passes aquecidos, todas as execuções equivalentes e com checagem visual),
lavapipe, 4 CPUs, 1024 draws + 8 resolves:

| Braço | Frame P50/P95/P99 (ms) | CPU do renderer (ms) | GPU ociosa (ms) | Razão pareada [IC 95%] |
|---|---|---:|---:|---|
| desligado (referência) | 91,4 / 117,6 / 123,5 | 33,8 | 36,6 | — |
| desligado, A/A (ruído) | 89,7 / 115,2 / 137,3 | 34,1 | 37,0 | 1,004 [0,925; 1,107] |
| ocioso, 64 | 94,7 / 111,4 / 129,5 | 34,8 | 22,5 | 1,03 [0,95; 1,13] inconclusivo |
| enfileirado, 64 | 149,5 / 187,1 / 201,7 | 109,4 | 23,0 | **1,66 [1,49; 1,76] mais lento** |

A ociosidade cai nos dois, mas no lavapipe a "GPU" roda nas mesmas CPUs que
gravam os comandos: sobrepor as duas multiplica o tempo de CPU do renderer.
**As duas estratégias ficam desligadas por padrão** (regra: nenhuma
configuração com regressão medida é ativada). O efeito numa GPU real, onde a
execução não disputa a CPU, só pode ser decidido pelo A/B do Skate 3 na
RTX 4070 (comando abaixo, braços off/idle64/queued64/idle256/queued256).
Dados: [JSON](render-core-eager-submit-2026-11-01.json).

### D3D12: estado gráfico redundante e busca de PSO sem alocação

Cada draw D3D12 reespecificava todo o estado (heaps, root signature, PSO,
argumentos de root, viewport, scissor, topologia, índice, render targets) e
procurava o PSO num `std::map` com chave `std::string` de uma cópia da
descrição inteira (alocação e comparações de centenas de bytes por draw). No
Skate 3 a fase "estado/constantes" custava ~9,3 ms/frame e o draw na CPU
6–7 µs por draw (RENDER-FINAL).

`D3D12Commands` agora não grava estado igual ao que a lista já tem. Regras
D3D12 preservadas: root signature diferente invalida os argumentos; heaps
diferentes invalidam as tabelas; o PSO é compartilhado com compute (dispatches
atualizam o cache); reset da lista limpa tudo; a gravação diferida repete a
mesma ordem. PSOs ficam num hash map com chave de tamanho fixo, e um draw
com a mesma descrição do anterior reutiliza a entrada sem hash.
Microbenchmark só da busca (descrição D3D12 de 656 bytes, 400 PSOs, 2800
draws com troca a cada ~4, melhor de 30, contêiner desta nuvem): 127 ns por
draw com `std::map<std::string>`, 87 ns com chave fixa, 42 ns com a checagem
do anterior (~0,24 ms por frame de 2800 draws).
`native_redundant_state_filter=false` grava tudo (A/B, diagnóstico) e
`native_stats` informa as chamadas evitadas (`redundant_state_skipped`).

Prova de equivalência: `native.d3d12-commands` compara o estado efetivo de
cada draw com e sem filtro numa lista falsa com as regras de invalidação mais
estritas (PSO diferido, compute no meio, reset); retirar a invalidação por
root signature faz o teste falhar (draws 5–7). O contrato D3D12 de
captura/replay reproduz a captura pesada também com o filtro desligado.
Tempo: A/B informativo no WARP do CI (filtro on/off, mesmos frames) — ver
"Testes"; sem medição em GPU real.

### Vulkan: constantes por draw e descriptor set reutilizados

Cada draw Vulkan enviava os cinco buffers de constantes e alocava e escrevia
um descriptor set novo (no lavapipe, alocação/reset de descriptors foi ~38%
das amostras da thread do renderer, por amostragem de pilhas com gdb). Como o
`DrawConstants` do D3D12 (commit `86c70bb`, RENDER-FINAL: −15% de CPU por
draw no Skate 3), um buffer cujos bytes finais são iguais aos do draw
anterior na mesma arena reutiliza o upload, e com os cinco iguais o descriptor
set anterior é ligado de novo. O cache é limpo com a arena dona dos uploads e
do pool.

Mesmos frames, build antes/depois, lavapipe (1024 draws + 8 resolves,
sintético, todas as execuções equivalentes): CPU do renderer 33,98 → 32,54 ms e
33,59 → 31,93 ms por frame em duas rodadas (7 e 9 pares); frame 0,90
[0,87; 0,95] e 0,94 [0,88; 1,04] contra A/A de 0,94 e 1,00 — **redução de CPU
repetida, ganho de tempo de frame não comprovado**. O workload repete as
mesmas constantes (melhor caso). Dados:
[JSON](render-core-vulkan-constants-2026-11-01.json).

### D3D12: limite de espera de fence configurável

O renderer e o scanout lançavam erro após 5 s fixos de espera. O WARP compila
os shaders ROV traduzidos no primeiro uso e o primeiro frame do CI excedia
esse limite ("render GPU timeout"), sem distinguir lentidão de travamento.
`native_gpu_timeout_ms` (padrão 5000, mínimo 1000) define o limite e o erro
informa os valores da fence; o CI Windows usa 120 s.

## Testes

| Teste | Plataforma | Resultado |
|---|---|---|
| `native.capture-format`: 7 casos / 95 asserções — ordem, deduplicação, present, payloads malformados, present sem imagem composta (0×0, meia, >16384) e versão 2 recusados, contrato do hash de imagem (vazia/truncada/stride inválido recusados, X ignorado), estrutura, todo truncamento e byte corrompido; ASan/UBSan limpos | Linux | aprovado |
| `native.capture-replay-vulkan`: efeitos `none` e `fxaa`, replay 3 passes = 12/12 scanouts, 12/12 faixas GPU, 48/48 contadores, 12/12 imagens compostas, `visual_checked`, repetível | Linux lavapipe | aprovado |
| Mesmo teste, controles: sem EDRAM (3), vértices alterados (3), presenter sem imagem `--drop-output` → "presenter returned no composed image" em 4/4 frames (3), arquivo truncado (2) | Linux lavapipe | detectados |
| Mesmo teste, workload pesado com submissão desligada, ociosa e enfileirada (7 → 8 → 9 submissões/frame) | Linux lavapipe | equivalentes, repetíveis |
| `native.d3d12-commands`: estado efetivo de 13 draws idêntico com e sem filtro (regras de invalidação estritas, PSO diferido, compute, reset); mutação sem invalidação por root signature falha | Linux (DirectX-Headers + shim, ASan/UBSan) e Windows CI | aprovado no Linux; Windows: ver PR |
| `native.pipeline-recording`, `native.pipeline-cold-vulkan`, `native.vulkan-components`, `sdkcheck.Native-MEMEXPORT-vulkan`, `sdkcheck.Native-FSI-sample-counts`, `sdkcheck.Native-FrameTiming` | Linux lavapipe | aprovados |
| Build Windows x64 Release do plugin D3D12, replay, workloads e testes (toolchain fixado, sem checagem de sintaxe) | Windows CI | aprovado |
| `native.capture-format`, `native.pipeline-recording`, `native.d3d12-commands` | Windows CI | ver PR |
| `native.capture-replay-d3d12` | Windows WARP (CI) | **não executável no WARP: pulado com motivo** (abaixo) |
| Sessão de captura (`native_capture_session.py`) com título simulado: gatilho, detecção do arquivo completo, replay equivalente e A/B de 5 braços | Linux | aprovado (sem jogo real) |

**Native D3D12 no WARP dos runners hospedados (investigado, sem solução nesta
entrega).** O WARP do Windows Server 2022 aceita as listas de comando do
Native, a camada de debug (Graphics Tools instalada no CI) não reporta nenhum
erro, e a primeira fence nunca completa quando a submissão contém um draw ROV
com pixel shader de cor do guest (`fence 0 of 1` após 120 s; com e sem
contagem de amostras). Draws sem pixel shader do guest (sonda de pipelines)
completam. O WARP redistribuível 1.0.22 (NuGet, SHA-256 fixado) com o runtime
D3D12 do sistema expõe um dispositivo sem faixas de descritores ilimitadas
(camada de debug: "unbounded array size … not supported … on a SRV level
device") e recusa a root signature, válida em hardware de tier 3; usá-lo
exigiria também o runtime Agility SDK correspondente. O mesmo caminho roda no
lavapipe e rodou na RTX 4070 (RENDER-FINAL). Hipótese: limitação do WARP na
execução do pixel shader ROV traduzido; não comprovada (sem validação da GPU).
Por isso `native.capture-replay-d3d12` é marcado **pulado** apenas com essa
evidência exata (adaptador WARP + primeira fence não concluída); qualquer
outra falha continua falhando, e em adaptador de hardware o contrato roda
completo. Erros D3D12 do Native agora trazem o HRESULT em hexadecimal e as
mensagens da camada de debug (`d3d12_debug=true`).

O job Windows compila de verdade o plugin D3D12, o replay e os testes com o
toolchain fixado (não é checagem de sintaxe) e executa os mesmos contratos com
`d3d12_adapter=-2` (WARP) e `native_gpu_timeout_ms=120000`. Os dois jobs
também rodam A/Bs informativos sobre a captura pesada (estratégias de
submissão; no WARP também o filtro de estado) e publicam os JSONs.

Limites conhecidos: MEMEXPORT com endereço dinâmico não entra na verificação;
texturas são gravadas pelos bindings (conservador); a decisão de visibilidade
do CP é gravada, não reexecutada; o replay não mede o PM4 nem a CPU do título;
WARP e lavapipe não representam o desempenho de GPU real.

## Skate 3 no Windows/RTX 4070 (D3D12) — teste local

Nada abaixo foi executado nesta nuvem (sem GPU real e sem o jogo): **nenhum
FPS é aprovado por esta entrega**. Build (um `-R` por chamada: `devshell.cmd`
transforma `|` em pipe):

```powershell
scripts\devshell.cmd cmake --build --preset win-amd64-release --target rexgpu-native x360n_native_replay x360n_native_frame_probe x360n_native_pipeline_tests
scripts\devshell.cmd ctest --preset win-amd64-release -R "native[.]capture" --output-on-failure
scripts\devshell.cmd ctest --preset win-amd64-release -R "native[.]d3d12-commands" --output-on-failure
```

Captura, validação e A/B numa sessão (o executável AOT do Skate 3 precisa
incluir o plugin deste build; seed congelado do RENDER-FINAL; tudo fica em
`.local\captures`):

```powershell
python scripts\performance\native_capture_session.py --exe .local\build\titles\skate_3\skate_3.exe --game-data-root <pasta do jogo usada no cenário> --user-seed .local\perf-gpu-pr9\seed-skate --out .local\captures\skate3\street-1 --trigger-on-enter --frames 3 --ab --title-arg=--mnk_mode=true
```

O script inicia o jogo com Native D3D12, perfil ao vivo e captura armada;
com a cena da rua na tela, Enter inicia a captura de 3 frames (ou
`--trigger-after SEGUNDOS` / `--skip-presents N` sem intervenção). Ele espera
o arquivo completo, fecha o jogo após `--profile-seconds`, reproduz a captura
10 vezes exigindo equivalência com checagem visual e roda o A/B dos braços
off, idle64, queued64, idle256 e queued256 nos mesmos frames.
`session.json` guarda argumentos e SHA-256 do executável, DLLs e captura.

Leitura: `replay\replay.json` (`per_frame`: draws, resolves, submissões,
fence, GPU ocupada/ociosa, `bottleneck`) e `ab-submission.json` (P50/P95/P99,
CPU do renderer, GPU, razão pareada com IC 95% e veredito). Uma estratégia só
pode virar padrão com veredito "faster" e sem piora de P95/P99.

A/B de dois builds (por exemplo antes/depois do filtro de estado) nos mesmos
frames:

```powershell
python scripts\performance\native_replay_ab.py --capture .local\captures\skate3\street-1\frames.x360nfc --output .local\captures\skate3\ab-builds.json --backend d3d12 --rounds 7 --repeat 10 --arm base <dir do build base> --arm candidate .local\build\win-amd64\src\native\Release
```

E o filtro de estado ligado/desligado no mesmo build:
`--arm unfiltered <dir> native_redundant_state_filter=false --arm filtered <dir>`.
