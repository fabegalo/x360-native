# RENDER-FINAL — trabalho em andamento

Base: `main` / `origin/main` `e837aafcbf4982e5774fde318789727ed636f709`.
Branch de trabalho: `codex/render-final`. Não há aceite de performance,
fidelidade integral ou superioridade ao Xenia Canary. O trabalho parte dos
relatórios PERF-GPU-01/02; as reduções anteriores de submissões, o cache de
texturas residentes e os workers de pipelines não são apresentados como novos.

## Condições das medições

Windows x64, GPU NVIDIA RTX 4070 identificada pelo runtime; executáveis Release,
compilação pelo `scripts/devshell.cmd` com LLVM 22.1.8. Não houve build nem probe
concorrente durante as rodadas abaixo. O usuário controlou o jogo; o agente
somente observou e coletou métricas, conforme autorização. Cada processo teve
limite de 275 s, com janela de 60 s iniciada aos 210 s. Nenhum input foi enviado
pelo agente. `native_stats` e contadores de performance estavam habilitados
nos dois lados instrumentados.

Console, saves e cache de aplicação partiram do mesmo fixture imutável
`.local/perf-gpu-pr9/seed-skate`, copiado para uma pasta isolada por execução.
Caches de arquivos do SO e do driver não foram controlados: estas rodadas **não
comprovam cache frio**. Os perfis e recursos 17559 permaneceram isolados.
Artefatos AOT/runtime são os já preparados em PERF-GPU-02; apenas o plugin
Native foi substituído nas candidatas. `environment.json` de cada rodada contém
hashes dos originais, executáveis, DLLs, seed, coletor e argumentos completos.

FPS e percentis medem cadência de `XE_SWAP` concluído, não scanout. CPU é a soma
dos tempos de todas as threads, não o caminho crítico. Primeiro frame é o
intervalo do runtime até o primeiro swap, não clique até gameplay. Os tempos
GPU internos abaixo vêm dos timestamps da fila Native e não substituem uma
captura validada por processo. O coletor WDDM excedeu seu timeout e não produziu
amostras; VRAM e tempo GPU do processo ficam indisponíveis, nunca zero. A tentativa
de amostragem PSS parou no orçamento de 5 ms após uma amostra de 6.849 us;
não constitui perfil estatístico. PIX/RenderDoc e camada Khronos de validação
não estavam disponíveis nos caminhos verificados.

## Skate 3 / Native D3D12

| Medida | `main` original | Controle instrumentado | Cache de SRVs |
|---|---:|---:|---:|
| FPS médio | 15,65 | 14,09 | 17,66 |
| 1% low (FPS) | 9,72 | 10,82 | 12,93 |
| P95 (ms) | 84,933 | 84,324 | 68,117 |
| P99 (ms) | 100,606 | 85,760 | 69,095 |
| CPU agregado (ms/frame) | 220,98 | 234,19 | 183,84 |
| Primeiro swap (ms) | 1.069,48 | 1.065,13 | 1.062,55 |
| Working set máximo (bytes) | 1.399.050.240 | 1.422.258.176 | 1.405.374.464 |
| Commit privado máximo (bytes) | 2.368.000.000 | 2.318.163.968 | 2.312.204.288 |
| Frames completos na janela | 938 | 844 | 1.059 |

Controle e candidata usam os mesmos timers por fase. O usuário confirmou
"Mesmo ponto, sem defeito percebido" para a posição na rua, câmera voltada
à árvore/parque no minuto final. Tráfego/NPCs e orientação exata não têm replay
determinístico. A candidata enviou 2.632,86 draws/frame, contra 2.839,51 no
controle; o aumento bruto de FPS de 25,33% não pode ser atribuído integralmente
à alteração. A captura final da candidata falhou depois que o processo fechou;
foi preservada a última captura anterior à janela, sem rotulá-la como prova do
minuto final. Guide e cena 3D foram observados, mas isso não certifica todos os
efeitos, MSAA, saves ou C7/C8.

| Contador interno por frame | Controle | Cache de SRVs |
|---|---:|---:|
| Draws | 2.839,51 | 2.632,86 |
| Resolves | 25,00 | 25,00 |
| Submissões | 6,00 | 6,03 |
| Texturas (ms) | 12,51 | 6,26 |
| Draw total, inclui texturas (ms) | 35,59 | 27,07 |
| Índices na CPU (ms) | 8,45 | 7,65 |
| Estado/constantes (ms) | 9,82 | 8,81 |
| Esperas de fence (ms) | 16,29 | 13,60 |
| Fila GPU ocupada (ms) | 17,12 | 14,63 |
| Intervalos ociosos da fila (ms) | 53,87 | 42,03 |

Categorias são inclusivas; não somar tempos de draw e suas fases. Contadores
cumulativos são reduzidos entre amostras internas dentro da janela; o intervalo
exato de cada série consta nos JSONs locais. O tempo de texturas por draw caiu
de aproximadamente 4,41 para 2,38 us. Não houve criação de PSO durante a janela
aquecida. Os dados direcionam a próxima correção à normalização/upload de
índices, sem concluir que resolves ou as cinco esperas observáveis sejam
dispensáveis.

Evidência local:

- `.local/evidence/benchmarks/render-final-base-d12-1/skate_3/`
- `.local/evidence/benchmarks/render-final-control-d12-1/skate_3/`
- `.local/evidence/benchmarks/render-final-srv-d12-1/skate_3/`
- `.local/render-final/{base,control,srv}-costs.json`
- `.local/render-final/screenshots/`

Plugins Native SHA-256: base
`cf270d516c2ee2aaa6025ff97bac5bd5d1bb228af52787c43a9bd20e234366fd`;
controle `dfa7c05052336fb82d39e5b618692bcf1176e234a42d19afe8cc6ce8d48e19a2`;
SRV `4ff3bd7b8ff85ffbd1e21637603cdc345eaee358445efa094ffd9975afec08bd`.
As três execuções encerraram no limite, sem swap failure. Houve 32 erros VFS
preexistentes (`big:/dlcbig`) e 90 warnings por rodada; não foram ocultados nem
classificados como sucesso de compatibilidade.

## Implementações e regressões

### Native D3D12: descritores de texturas

SRVs imutáveis são reutilizados por arena, com chave de recurso e descrição
completa da view. A arena retém recurso e heap até sua fence. Swizzle, dimensão
e mip continuam fazendo parte da identidade; rotação não reutiliza descritores
em voo. O teste GPU executa 6.000 draws residentes, alterações CPU, swizzles
simultâneos, textura nula e quatro rotações, conferindo MEMEXPORT byte a byte.
Controle antigo falha pela submissão por capacidade; candidata passa sem essa
submissão, com zero views novas e 12.000 reusos no trecho residente. A duração
isolada anterior (78,6 → 82,6 ms) não é comparação válida: houve build Xenos
concorrente. O ganho de gameplay acima é a evidência temporal disponível.

### Native D3D12: índices residentes

Commit `8a46545` também permite que o Input Assembler leia índices DMA do
espelho coerente de memória GPU. A tradução existente aplica endianness antes
do offset/clamp. R16 com troca de pares, conversão de topologia, restart,
tessellation e MEMEXPORT mantêm normalização CPU. A transição inclui os dois
usos compatíveis de leitura (IA e shader); uploads e escritas continuam
ordenados. Limites e alinhamentos são conferidos antes da escolha do caminho.
Não há leitura CPU de índices elegíveis nem upload redundante desses valores.

Regressão Release: 20 pares de pixels idênticos entre índices normalizados e
residentes, R16/R32 e endians, extensão unsigned, offset/clamp, strip/fan/restart,
modificações CPU, produtor MEMEXPORT, rotação de arenas e limites inválidos.
Workers 2 e 0 passaram; controle CPU falhou no requisito de uso residente.
O primeiro build do teste falhou por include ausente e a primeira fixture
produziu preto por blend configurado como zero; ambos foram corrigidos antes
das execuções válidas. Nenhum binário de build falho foi aprovado.
Evidência: `.local/render-final/d3d12-index-plugin-pair-v3/result.json` e
`.local/render-final/d3d12-index-v2-{red,green,inline}/`.
Gameplay de índices: `render-final-indices-d12-1`, mesmos 210+60 s e seed,
1.159 frames completos: **19,34 FPS**, 1% low **14,59 FPS**, P95 **66,594 ms**,
P99 **68,290 ms**, CPU agregado **164,15 ms/frame**, primeiro swap **1.112,31 ms**.
Working set máximo 1.407.324.160 bytes; commit privado 2.314.285.056 bytes.
A cena da rua/parque foi capturada dentro da janela. Foram 2.826,26 draws/frame,
mais que os 2.632,86 da rodada SRV. Índices CPU: **7,65 → 0,286 ms/frame**;
draw total: **27,07 → 20,88 ms/frame**. Normalização: 1.350.936,96 → 499,17
índices/frame. Estado/constantes ainda 9,27 ms, fence 13,62 ms; 6,10 submissões
e 24,99 resolves/frame. Continua insuficiente para aceite.

Nesta rodada foi acrescentado PresentMon 2.6.0, autorizado como administrador
pelo mantenedor após falha ETW por acesso negado na conta normal. SHA-256 oficial
`b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af`.
Somente PID do jogo, máximo 270 s, sem instalação global/serviço ou mudança de
grupos. O custo desse coletor não estava nas rodadas anteriores; por isso os
próximos pares usam o mesmo coletor nos dois lados. Seu campo de data mostrou
deslocamento extra de três horas; as próximas capturas usam QPC ancorado no
relógio do processo, sem tratar esse campo de data como UTC correto.

ETW capturou 66.631 apresentações host em 245,72 s, num único swapchain,
enquanto os swaps guest mediam 19,34 FPS na janela. A interface redesenha perto
da taxa do monitor (CIM: 1920×1080, 279 Hz; driver 32.0.16.1742). **Essas
apresentações repetidas não são FPS do jogo.** O toast de achievements vazio
permanece registrado no ImGui e é uma hipótese concreta de custo de UI/fila
compartilhada, em investigação. As duas fontes ficam separadas.

### Native Vulkan: resolve, textura e visibilidade

Texturas carregadas na GPU, sem escala, usam a memória residente após resolve;
não exigem publicar primeiro seus bytes na CPU. Conversão pela CPU e Enhanced
mantêm a sincronização necessária. A dependência de buffer agora retém o último
escritor até cada novo par estágio/acesso leitor receber visibilidade; leitores
acumulados continuam precedendo a próxima escrita. A regressão modela compute
write → transfer read → shader read e pares distintos VS/FS, além do teste GPU
resolve → texture fetch → MEMEXPORT. O modelo antigo falha; não foi observada
corrupção na GPU antiga, portanto o modelo não é descrito como captura visual.
Controle GPU antigo força publicação CPU indevida e falha; candidata passa em
1x/2x e com workers síncronos/assíncronos. Evidências em
`.local/render-final-vulkan/`.

Commit `914a816`: loads de texturas usam janelas alinhadas de storage buffer
quando 512 MiB excedem o limite do dispositivo. Fonte, mips, slices e scratch
recebem offsets relativos, sem modificar os shaders pinados. O caminho de
buffer inteiro continua disponível quando permitido. Pools adicionais de
descritores pertencem à arena e só são reciclados após sua fence, evitando
flush no meio de um draw e recuperando falta de espaço/fragmentação.

Release aprovado (87,2 s). Controle anterior falha por descritor excedente;
controle de pool falha com `VK_ERROR_OUT_OF_POOL_MEMORY`. Candidatas workers 2/0
e caminho de buffer inteiro passam, conferindo float4 em texturas lineares,
tiled, volume, mip separado, array 9×16 MiB, scratch de 144 MiB e rotação de arena.
**128 MiB de limite e oito sets/pool foram emulados na RTX 4070**: limite real
4.294.967.295 bytes. O teste não comprova execução em outro hardware.
Evidências `ranges-{red,green,green-inline,full-buffer}`, `pools-red` e
`build-ranges-result.json` em `.local/render-final-vulkan/`. Gameplay Vulkan
continua pendente.

### Xenos: comparação direta com Canary

Referência: [Canary 44e6489](https://github.com/xenia-canary/xenia-canary/releases/tag/44e6489),
commit `44e6489cf87567b5dc9c982207765d722b71b966`, verificado em 31/10.
O executável oficial foi obtido e seu SHA-256 é
`16beefa76a12e01459023fa50089f039de4b59ca8adbb81eb444e36af62813e0`.
Ainda não executado nesta etapa. Deduplicação de texturas e gamma já estavam
presentes; não foram reapresentadas como correções novas.

O patch 0246 adapta os snapshots MEMEXPORT antes/depois de
[D3D12 0bfc8fa](https://github.com/xenia-canary/xenia-canary/commit/0bfc8faf7da90c7619123f3d7338965828e03469) e
[Vulkan 1d8489b](https://github.com/xenia-canary/xenia-canary/commit/1d8489b44e08f1e85e69a746f24b0d17a0dd6fe8).
Publicar somente bytes alterados preserva vizinhos que a CPU modificou, mas
não fornece máscara exata de stores idempotentes concorrentes no mesmo byte.
Snapshots dobram bytes transferidos/readback; não há alegação de ganho Xenos.
Slots são protegidos por fences desde a primeira cópia; o modo síncrono
reutiliza uma única alocação, sem reter duas por faixa histórica.

Build final Release aprovado; 13 asserções em três casos CPU e quatro modos
GPU aprovados: D3D12 fast/full e Vulkan fast/full. Os controles negativos
finais falham no byte vizinho na iteração 1 (D3D12 fast) e 0 (Vulkan full
concorrente). Vulkan full percorre oito streams distintos sem novas alocações
após o primeiro draw. A primeira expectativa de uma alocação global falhou
porque também contava a criação preguiçosa de occlusion query; a fixture final
compara o total após o primeiro e último draws (2/2), sem relaxar preservação
de bytes. O debug layer D3D12 não foi habilitado pelo ambiente.

Plugin final SHA-256
`6a225cddf46e8cdf15562b8832142404c74c81d5edeb0aada50054f18e3c82be`;
evidências em `.local/render-final/xenos-final-*`. Replay isolado LF aplicou
os cinco arquivos com hashes idênticos aos usados no build. Patch e lock foram
integrados pelo `scripts/setup/apply-sdk-patches.py`; `--check` aprovou os 233
patches ativos. Isso preserva o SDK pinado e não constitui build dos seis alvos.

### Native D3D12: constantes por draw

Commit `86c70bb`. Cada root de constantes (sistema, floats VS/PS, bool e fetch)
compara os bytes empacotados finais e a extensão com o upload anterior; sendo
idênticos, reutiliza o endereço GPU. Os endereços são descartados a cada
reciclagem da arena; mudança de mapa de shader, banco de floats, sistema ou
registradores é detectada exatamente pelos bytes, sem confiar em dirty bits.
Contrato GPU Release `constant-contracts`: 64 draws idênticos sem novo upload
nas roots 0/1/3/4, mudanças de sistema/bool/fetch geram upload, bancos e mapas
VS/PS distintos, uploads enfileirados imutáveis através de resolves compute e
rotação de arenas. Candidata passa com workers 2 e 0; controle (uploads
forçados) falha em "identical draw constants allocated another upload".
Fontes conferidas como anteriores ao binário testado. Evidência:
`.local/render-final/d3d12-constant-{green,inline,red,plugin-pair}/`.

Gameplay em dois pares ABBA (controle 1, candidata 1, candidata 2,
controle 2; `render-final-constant-{control,candidate}-d12-{1,2}`): mesmos
binários exceto o plugin Native (`f40f4010…` candidata contra `63ce79f8…`
controle), mesmo seed e cenário, janela 210+60 s, limite de 275 s, PresentMon
autorizado pelo mantenedor e restrito ao PID do jogo. O usuário jogou no mesmo
trecho de árvores/parque e o agente só observou:

| Medida | Controle 1 | Candidata 1 | Candidata 2 | Controle 2 |
|---|---:|---:|---:|---:|
| Frames completos na janela | 1.140 | 926 | 1.247 | 1.182 |
| FPS médio | 19,02 | 15,46 | 20,81 | 19,72 |
| 1% low (FPS) | 8,62 | 13,91 | 9,74 | 14,64 |
| P95 / P99 (ms) | 66,921 / 68,644 | 69,079 / 70,155 | 51,711 / 52,530 | 52,652 / 67,565 |
| CPU agregado (ms/frame) | 169,96 | 189,91 | 146,06 | 153,25 |
| Primeiro swap (ms) | 1.262,75 | 1.205,44 | 732,88 | 754,75 |
| Working set / commit privado máximos (MB) | 1.456,8 / 2.362,1 | 1.416,2 / 2.291,6 | 1.362,5 / 2.229,7 | 1.304,7 / 2.176,0 |
| Draws por frame | 2.911 | 3.007 | 2.681 | 2.810 |
| Draw total na CPU (ms/frame) | 21,73 | 18,97 | 15,90 | 19,88 |
| Draw na CPU por draw (µs) | 7,46 | 6,31 | 5,93 | 7,08 |
| Constantes enviadas (MB/frame); reuso | 5,78; 0% | 2,43; 80,5% | 2,18; 80,8% | 5,40; 0% |
| Fila GPU ocupada (ms/frame) | 14,80 | 25,31 | 12,56 | 13,68 |
| Fila GPU ocupada por draw (µs) | 5,09 | 8,42 | 4,68 | 4,87 |
| Esperas de fence (ms/frame) | 13,85 | 25,95 | 11,84 | 12,85 |
| Criação de PSO na janela (ms/frame) | 1,00 | 0 | 0,93 | 0 |
| PresentMon: apresentações host/s; GPU ocupada (ms/s) | 267,1; 382,5 | 240,7; 570,3 | 278,0; 365,1 | 272,6; 368,9 |

Reuso por root na candidata 1: sistema 97,3%, floats VS 87,4%, floats PS
91,2%, bool 99,8%, fetch 30,1%; a candidata 2 repete a proporção (80,8% no
total). O alto custo de GPU da candidata 1 (8,42 µs/draw, 570 ms/s no
PresentMon) **não se repetiu**: a candidata 2 ficou em 4,68 µs/draw e
365 ms/s, abaixo dos dois controles (4,87–5,09 µs/draw; 369–383 ms/s). Isso
é coerente com os três pares headless alternados (`batch`, 4.096 draws
idênticos, mesmos binários dos contratos), sem aumento de GPU atribuível ao
cache: GPU controle 18,90/19,61/18,06 ms, candidata 16,55/15,42/16,11 ms;
wall 52,1/57,0/52,1 contra 45,9/45,5/45,0 ms. A causa da candidata 1 continua
**hipótese de cena** (câmera/tráfego sem replay determinístico), não
comprovada. O efeito que se repetiu nos dois pares foi o custo de draw na CPU
por draw: −15,4% no par 1 e −16,2% no par 2, com 58–60% menos bytes de
constantes por frame. FPS e 1% low não acompanharam esse custo de forma
consistente (par 1: −18,7% FPS e +61% 1% low; par 2: +5,5% FPS e −33% 1% low),
então, com duas rodadas manuais por lado, **nenhum ganho ou perda de FPS é
atribuído a este commit**. Evidência:
`.local/render-final/constant-{control,candidate}{,-2}-costs.json`,
`.local/render-final/constant-gpu-pairs/`, `.local/render-final/presentmon/`.

### Native Vulkan: índices residentes

Commit `d3c64ea`. Mesmo critério do D3D12: R16/R32 alinhados são lidos pelo
Input Assembler direto do buffer coerente da memória guest; o shader traduzido
aplica o endian antes de offset/clamp. Restart, conversão de topologia,
expansão, tessellation, MEMEXPORT, R16 com trocas 8in32/16in32 e hosts sem
`fullDrawIndexUint32` mantêm a normalização na CPU. A leitura de índices
recebe sua própria visibilidade do último escritor (upload CPU, resolve ou
MEMEXPORT). Contadores opcionais de draws/índices no log a cada 120 frames.

Contrato Release `index-contracts` (RTX 4070): 20 pares de pixels idênticos
entre caminho normalizado e residente, R16/R32 com os quatro endians, valores
altos, offset/clamp, strip/fan/restart, escritas CPU, produtor MEMEXPORT sem
submissão nem readback e limites inválidos rejeitados. Candidata passa com
workers 2 e 0; controle (draw do HEAD anterior) falha em "resident index
path/fallback selection changed". Hashes em
`.local/render-final-vulkan-indices/sha256.txt`. Sem gameplay Vulkan.

### Xenos Vulkan: depth bias em unidades constantes (0247)

Commit `2b872af`, patch 0247, adaptação do Canary
[a8d034c](https://github.com/xenia-canary/xenia-canary/commit/a8d034c00df4570f68fa14186cbde8c6f172870a).
O teste do probe GPU não compilava (`vkDeviceWaitIdle` ausente na tabela do SDK);
a limpeza passou a esperar a fila usada. `sdkcheck.vulkan-depth-bias`: 65
asserções em 3 casos. Probe opt-in `x360n_vulkan_depth_bias_gpu` na RTX 4070:
modo correto 24/24 casos exatos (D32 float24 e D24, sinais, profundidades
altas/baixas, viewport invertido); controle legado 18/24 falhas, com o offset
dobrando ou encolhendo conforme a profundidade; dispositivo sem as features
opcionais usa o comando antigo. Os oito arquivos aplicados ao SDK são idênticos
aos testados; `apply-sdk-patches -Check` aprovou o lock. Evidência:
`.local/render-final/depth-bias-runs/`. Não comprova shaders completos nem
fidelidade em jogo.

### UI: toast de achievements ocioso (0248)

Commit `7d5f77e`, patch 0248. Confirma a hipótese acima: o diálogo ficava
registrado no `ImGuiDrawer` durante toda a sessão e uma lista de diálogos não
vazia mantém o presenter redesenhando continuamente. Agora o toast sai da
lista quando a fila esvazia e um evento agenda um único despertar coalescido
na thread de UI. Callbacks de threads guest são encaminhados à UI; destruição
cancela despertares pendentes. Durante a validação apareceu um defeito
preexistente: a idade em `float` de exatamente 4,5 s arredonda para
4,4999995 s e cada toast ficava um frame a mais; a expiração passou a comparar
a duração exata do relógio.

Contrato com frames ImGui reais e drawer CPU: 227 asserções em 5 casos, três
execuções seguidas aprovadas (lista vazia em repouso, quatro produtores
concorrentes com um só despertar e 4,5 s por evento, cancelamento no
desligamento, evento durante a remoção, posse do callback na UI). Controle
falha porque o toast ocioso mantém a lista não vazia. `rex_app.cpp` modificado
compilado isoladamente. Evidência: `.local/render-final-ui/`. **Não foi medido
o efeito sobre apresentações host nem FPS**; a captura PresentMon antes/depois
continua pendente.

### Build oficial dos novos testes

Configure `win-amd64` a frio (411 s, patches do SDK verificados) e build
Release só de `x360n_sdk_runtime_checks`, `x360n_vulkan_depth_bias_tests` e
`x360n_vulkan_depth_bias_gpu` (3,9 min), com o SDK já contendo 0247/0248.
Execução focada nos binários oficiais: casos `[achievement-toast]` 227
asserções/5 casos; `sdkcheck.vulkan-depth-bias` 65/3; probe GPU correto 24/24
exatos, legado 18/24 falhas, fallback aprovado. Suítes completas não foram
executadas. Saídas em `.local/render-final/official-runs/`.

## Pendências de aceite

- Replay determinístico (ou cena fixa) para medir FPS das constantes D3D12:
  dois pares manuais ABBA repetiram a redução de CPU por draw, mas não um
  efeito de FPS.
- Par controle/candidata com 0248: exige reconstruir o executável AOT de Skate 3,
  porque `rex_app.cpp` é compilado em cada título e o layout de
  `AchievementToastDialog`/`VulkanDevice` mudou; trocar só `rexruntime.dll`
  não é seguro.
- Continuar investigando estado/constantes (9,27 ms) e esperas de fence
  (13,62 ms) do Native D3D12 conforme os resultados.
- Validar as próximas candidatas Release e registrar os demais commits.
- Gameplay nos quatro títulos e quatro caminhos renderer/API, com cenas
  equivalentes Canary, caches frios/aquecidos e medições repetíveis. Originais
  de Forza Horizon e Motocross Madness existem; seus executáveis AOT precisam
  ser preparados. Não marcar jogos como ausentes.
- Captura GPU/VRAM validada, estabilidade prolongada e avaliação de geometria,
  iluminação, sombras, transparências, pós-processamento, MSAA, HUD e Guide.
- Builds/execução Linux e macOS, x64/ARM64, e Windows ARM64 não demonstrados
  por estes testes Windows x64. Não inferir suporte dos seis alvos.
- Diferenças genéricas Canary ainda não incorporadas incluem strips indexados
  tessellated; a opção 40 MiB EDRAM voltada a patches por jogo não é habilitada
  como correção de fidelidade. Depth bias Vulkan foi incorporado (0247).

Este relatório registra o avanço verificável e os resultados negativos. Não
encerra a missão nem aprova performance com base em microbenchmarks.
