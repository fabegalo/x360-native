# Xenos — resolves vazios

Investigação de 2026-10-15 sobre `dd3113e`, isolada na branch
`codex/xenos-forza-correctness`. SDK e LLVM pinados preservados. Native e Vulkan
fora do escopo; nenhuma alteração na interface desktop.

## Causa e correção

Os relatórios P2/P3 e o run Xenos `20261015T221603Z-n4-bench-n41-xenos`
registram `Resolve region is empty`, seguido de falha de `PM4_DRAW_INDX_2`.
`GetResolveInfo` aplica a regra top-left, window offset e scissors, expande
para múltiplos de oito, e só então verifica se a região está vazia. Há dois
problemas distintos:

- Uma região vazia alinhada retorna falha e pode disparar assert em Debug,
  apesar de não haver pixels para copiar ou limpar.
- Uma região vazia desalinhada pode ser expandida para uma região não vazia,
  produzindo cópia/clear de pixels que não estavam cobertos. Reproduzido em
  teste sintético; ocorrência desse segundo caso no Forza não está comprovada.

O patch `0076-empty-resolve-noop.patch` verifica a cobertura vazia antes do
alinhamento e retorna sucesso com extents zero, inclusive após clipping ao
surface pitch. Reutiliza a saída existente de `D3D12RenderTargetCache::Resolve`
antes de qualquer comando copy/clear. Mantém a validação de comando, formato
do vertex buffer e MSAA; mantém o diagnóstico de região fora do surface pitch.
Não remove sincronização, não cria cache e não altera o limite de FPS.

Não é uma correção geral dos erros GPU do jogo. O fetch constant 90 inválido
permanece. O dump real mostra que o nome disassemblado é `vf5` (95−90), usado
em fetch predicado após `setp_eq` de uma constante do shader. Isso invalida a
hipótese inicial de fetch sem leitura; não demonstra que o acesso é inativo
em todos os draws. Não foi habilitado `gpu_allow_invalid_fetch_constants`.
Resolver esse caminho exige tratar corretamente predicação/residência, sem
transformar todo buffer inválido em leitura permitida.
Avisos de texture fetch inválido também existem nas evidências anteriores;
não foram classificados como inofensivos nem alterados. Não se demonstrou um
gargalo de sincronização que justificasse retirar waits/barreiras nesta tarefa.

## Testes focados

`sdkcheck.empty-resolve` exercita a função real com memória/registradores
sintéticos, sem conteúdo proprietário: largura/altura nulas, coordenadas
alinhadas e desalinhadas, cobertura subpixel, clipping por scissor e pitch,
copy/clear habilitados, e regiões não vazias em escalas 1/2 e MSAA 1/2/4.
Inclui os centros de pixel D3DZero e OGLHalf.

- Antes: 204 verificações, 84 falhas.
- Depois: 204 verificações, zero falhas.
- Logs: `.local/logs/xenos-test-{before,after}.log`.
- Patch aplicado/verificado pelo mecanismo normal, com hashes antes/depois
  e SHA-256 do patch em `deps.lock.json`.

Compilação focada via `scripts/devshell.cmd`: comandos do Ninja da preparação
existente, recompilando apenas `draw.cpp` e relinkando o plugin com os objetos
inalterados. Objetos/bibliotecas originais não foram sobrescritos. Comandos e
binários locais em `.local/validation/xenos-forza/build/`; log
`.local/logs/xenos-after-build.log`. O configure isolado completo foi interrompido
durante sondagens de dependências, sem usar resultado de build incompleto.

## Evidências de execução

A primeira coleta `xenos-fix-before` serviu para reproduzir os avisos e capturar
shaders. Chegou ao gameplay real, mas não serve como baseline de performance:
houve configuração de build em paralelo e dumping de shaders habilitado.
Os dumps e screenshots permanecem locais, fora do commit.

A comparação separada usa `xenos-measured-before` e `xenos-measured-after`,
com o mesmo AOT, DLLs de runtime, seed, inputs, roteiro e instrumentação.
Apenas o plugin Xenos difere. A janela é 240–270 segundos, sem builds
concorrentes. Os encerramentos do runner são limitados e forçados; não provam
shutdown limpo nem o ciclo pela dashboard. GPU via WDDM é utilização amostrada,
não tempo GPU/frame. Um par não certifica ganho geral ou campanha completa.

**Esse par de gameplay foi recusado para performance.** As capturas mostram
a mesma progressão da cinemática para o Golf, mas câmera externa antes e cockpit
depois. O comparador registra `comparable=false` em
`.local/validation/xenos-forza/gameplay-comparison.json`; os números não foram
descartados nem apresentados como ganho. FPS 28,066 → 28,964; CPU/frame
107,944 → 102,283 ms; P99 52,757 → 51,499 ms, **sem atribuição causal**.

A janela de logs é coberta integralmente pelos arquivos retidos nos dois lados
(`log-window.json`, 30 segundos a partir de 240 s de vida do processo):

| Diagnóstico | Antes | Depois |
|---|---:|---:|
| Resolve vazio reportado como erro | 5.054 | 0 |
| Falha correspondente `PM4_DRAW_INDX_2(3, 8, 2)` | 5.054 | 0 |
| Fetch constant 90 inválido | 2.130 | 2.320 |
| Falhas de backend restantes | 7.184 | 2.320 |

As contagens de fetch não são normalizadas por draw nem comparáveis como
regressão de frequência entre câmeras. Evidenciam que o erro remanescente não
foi silenciado. Não houve falha de swap ou critical nas coletas corrigidas.
As capturas mostram cena 3D, HUD, retrovisores e progressão, sem evidência de
nova corrupção; não são comparação pixel a pixel nem certificação de todos
os efeitos. Entrada Start/A aceita no roteiro real, sem bypass de perfil.

## Comparação controlada curta e áudio

`xenos-title-before/after` usa o mesmo cenário sem entradas e janela 45–65 s.
O nome/descrição inicial esperava tela de título, mas a inspeção mostrou o
**filme de abertura e seu fade** em ambos. A descrição original foi preservada
nos metadados e a revisão registra a cena real. Não é benchmark de gameplay.
`intro-comparison.json` aceita ambiente, seeds, originais, coletores e binários;
somente `rexgpu-xenosrd.dll` mudou. Os filmes não estão sincronizados por frame.

| Métrica da introdução | Antes | Depois |
|---|---:|---:|
| FPS de swaps guest | 30,002 | 30,025 |
| 1% low | 19,778 | 19,918 |
| CPU agregada por frame | 54,831 ms | 54,688 ms |
| P95 | 49,046 ms | 48,987 ms |
| P99 | 50,030 ms | 49,627 ms |
| Working set amostrado máximo | 923.639.808 B | 914.534.400 B |

Diferenças pequenas de um único par: **nenhum ganho global de desempenho
demonstrado**. O limite original de 30 FPS permanece. A coleta WDDM tentou
obter GPU/VRAM por PID, mas CIM estava indisponível no sandbox; arquivos
`gpu-before.json` e `gpu-after.json` registram falha e nenhuma amostra, não zero
de utilização. Não há tempo GPU/frame disponível.

Áudio pós-correção confirmado no endpoint de saída pelo medidor WASAPI da
sessão do PID 43312: 50/50 amostras não zero, pico 0,068731 (`audio-after-host.log`).
O helper local rodou por cinco segundos no host porque a ativação da interface
de sessão não estava disponível no sandbox. Não capturou microfone, não enviou
input global e não modificou o runtime. Isso confirma sinal de saída, não uma
avaliação humana da qualidade sonora.

## Escopo e preservação

Regressão compartilhada focada de Castle Crashers:
`.local/benchmarks/xenos-resolve-castle/castle_crashers/`. Start aceito e menu
original animado nas duas capturas, 59,968 FPS, 1% low 46,833, P95 18,243 ms,
P99 19,756 ms. Zero erros, criticals ou falhas de swap; três warnings. Não é
benchmark A/B de Castle nem validação de gameplay ou novo ciclo pela dashboard.

O ciclo integral Dashboard → My Games → Forza → Guide → Dashboard já aprovado
em `validation-forza-horizon.md` permanece como evidência histórica; não foi
repetido nem recertificado com esta DLL. Não houve mudança no loader, handoff,
XAM, C7/C8, identidade de perfis, saves, biblioteca ou originais. Os runners
usaram cópias da seed; o checkout principal e seus artefatos preparados não
foram substituídos. Não foram produzidos recibos artificiais para os binários
diagnósticos. Integração futura do commit deve usar a preparação normal.

Artefatos locais deste trabalho ficam sob `C:/Users/fabeg/Desktop/x360/.local/`,
mesmo estando o código no worktree `xenos-forza`. Screenshots estão em
`benchmarks/xenos-measured-{before,after}/forza_horizon/{before,after}.png` e
`benchmarks/xenos-title-{before,after}/forza_horizon/{before,after}.png`.
Nenhum conteúdo proprietário foi incluído no commit. Sem Computer Use,
push, Release, implementação Native ou trabalho Vulkan.
Hashes dos binários e principais evidências: `validation/xenos-forza/evidence.json`
sob a mesma raiz `.local`.
