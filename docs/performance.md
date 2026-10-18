# Performance-first, correctness-preserving

Performance é objetivo central do x360-native. A recompilação estática PowerPC
para x64 permite investir em AOT e runtime compartilhado, preservando os originais.
Toda melhoria exige medição antes/depois. Não reduzir fidelidade, omitir erros,
misturar módulos de versões, alterar semântica PPC/XAM, retirar sincronizações
necessárias ou usar stubs para obter números melhores. Preservar C7/C8, perfis,
saves e biblioteca. Compilador, AOT, CPU, memória, GPU e carregamento são frentes
contínuas, priorizadas por evidência.

O desenvolvimento Native está suspenso por decisão do mantenedor em 2026-10-15.
A prioridade atual é fortalecer o Xenos existente, sem iniciar trabalho Native
ou Vulkan. A investigação de resolves está em
[Xenos: resolves vazios](reports/graphics/xenos-empty-resolves.md) e
[X2: vertex fetch predicado](reports/graphics/xenos-fetch-predication.md).

Histórico: Native foi a prioridade gráfica iniciada em 2026-10-14: primeiro scanout D3D12
executado, com fronteira própria para Vulkan futuro e Xenos mantido como padrão.
[Resultados e limites](reports/graphics/native-renderer.md). A comparação de pixels passou;
o fixture com readback não é benchmark de FPS. A meta competitiva frente ao
Pinyon exige cenas equivalentes, mesma fidelidade, 1% low/P95/P99, CPU/GPU,
RAM/VRAM e pacing, sem mudar física/áudio/simulação. Ainda não há resultado
competitivo. Native N3 demonstrou dashboard e ciclo Castle em modo trial, mas
continua lento; Forza Native não está certificado. [Resultados N3](reports/graphics/native-renderer-n3.md).
P1/P2/P3 preservados.

## Launcher desktop

O launcher desktop mantém I/O e autenticação fora da thread Win32 e reutiliza
digests apenas durante lotes com exclusão de escrita/remoção. A comparação
sintética alternada reduziu a conferência de 21,60 para 11,56 s; não mede startup
real ou desempenho dos jogos. Instrumentação, regressões, limites e roteiro
manual: [launcher em 18/10/2026](reports/launcher-performance-2026-10-18.md).
No complemento com o acervo local, a espera inicial de 84–85 s caiu para 5,98 s:
abertura lista metadados, sem autenticar antecipadamente todos os títulos.
SHA-256/recibos continuam obrigatórios antes de preparar/iniciar; a verificação
completa também pode ser solicitada explicitamente. Não é medição de startup guest.

## Preflight de lançamento

**Configuração de produção:** frontend, supervisor, extrator e preparação normal
de títulos/SDK/DLLs exigem Release. Debug/RelWithDebInfo são diagnósticos explícitos.
Os benchmarks abaixo preservam as configurações históricas; não extrapolar suas
latências para o pacote final. [Política, correções e validação de Release](reports/release-configuration-2026-10-18.md).

O ambiente local foi posteriormente migrado, com aceite final reduzido a 17559
e Castle em Release. Ambos executaram smokes de 30 s e registraram swaps, com
preflight de 13,354 e 5,498 s, respectivamente. O conjunto de títulos preparados
e os binários mudaram; esses tempos não formam um novo antes/depois equivalente
e não medem clique até imagem. Situação dos demais alvos, gates e preservação:
[migração Release local](reports/local-release-migration-2026-10-18.md).

A continuação sobre `99a7764` reduziu a validação anterior ao processo em 38,13%
na 17559, 16,47% em Charlie, 26,83% em Moto e 15,32% em Castle, em comparação
alternada no mesmo acervo. Read-ahead de até quatro hashes grandes preserva
exclusão de escrita/remoção; inventários compartilham enumeração somente dentro
da mesma chamada. Dois fontes do verificador usam `-O2` em Debug com asserts,
símbolos e CRT Debug preservados. Não há aprovação persistente por metadados.

Os smokes curtos autorizados executaram os quatro títulos: três registraram
swaps; Charlie retornou à dashboard após bootstrap NetCF. Não certificam gameplay
nem tempo de clique até imagem. Os ganhos não eliminam os hashes integrais ou
o custo de handles/inventários e acrescentam memória/leitura especulativa em
alguns caminhos. Controle antigo com `-O2`, experiência sem ganho, I/O e resultados:
[preflight de 18/10/2026](reports/startup-preflight-2026-10-18.md).

## Builds incrementais

A otimização seguinte reduziu a reconstrução nativa completa da 17559 em 21,40%
e os no-ops de 17559/Charlie/Moto/Castle em 31,69–32,96%. O núcleo de fingerprints
de Motocross caiu de 68,26 para 10,75 s com 1.166 produtos idênticos. O lançamento
usa outra melhoria: uma transação protegida no supervisor, sem rehashes de bytes
já retidos e sem nova normalização dos mesmos nomes. A validação anterior ao
processo caiu 24,03% / 12,51% / 12,25% / 6,09%, respectivamente. O custo novo de
reler o catálogo está incluído; o caminho padrão de abertura foi mantido.
Resultados negativos, memória adicional, metodologia, commits e procedimento
manual de janela/primeiro frame:
[preparação e startup](reports/preparation-startup-2026-10-18.md).
O complemento corrigiu dependências Ninja obsoletas em objetos AOT genéricos
longos e a criação indevida de marcador ao regenerar projeto idêntico. O teste
real preservou 34/5 DLLs de Charlie/Moto e compilou só um objeto host; o custo
restante dos inventários/hashes ainda manteve Charlie em cerca de 123 s, sem
ganho de parede no incremental estável medido.

O driver de títulos reutiliza produtos certificados por conteúdo e dependências
reais do Ninja. Configuração, codegen e compilação têm decisões separadas; estado
inválido exige rebuild seguro. Metodologia, contagens, tempos e limites:
[builds incrementais de 18/10/2026](reports/incremental-builds-2026-10-18.md).

## Instrumentação

O patch 0070 reutiliza `rex::perf` e seu CSV. `perf_log_csv` passa a ser conectado
à inicialização. Sem `--perf_log_csv=<arquivo>` ou `--perf_counters=true`, os
contadores ficam desligados. O suporte funciona também em Release;
`REXGLUE_ENABLE_PERF_COUNTERS=OFF` remove os callsites existentes. O snapshot é
feito no XE_SWAP, depois de IssueSwap. Não há polling de UI para inferir FPS.

Cada linha registra timestamp monotônico desde Runtime::Setup e tempo acumulado
de CPU do processo (GetProcessTimes no Windows). CSV bufferizado, flush a cada
60 swaps; encerramento forçado pode perder o final. O runner conserva margem
após a janela e verifica cobertura. CSV e CPU por frame só são coletados quando
habilitados. Configurar antes dos workers; flush após o produtor parar.
Intervalos que cruzam as bordas são excluídos dos percentis; o JSON informa
`boundary_excluded_ms` e `window_coverage_fraction`. Uma janela só é completa se
o CSV cobre início/fim e os intervalos inteiros somam pelo menos 95% da duração
pedida. Perda maior nas bordas permanece registrada e invalida a comparação,
em vez de transformar uma longa espera atravessando o início em FPS artificial.
O gravador termina cada registro com newline. Um último fragmento sem newline,
possível no cleanup forçado, é descartado e contado em
`discarded_unterminated_records`. A cobertura continua obrigatória; linhas
internas inválidas e timestamps não monotônicos não são ignorados.
Essa validação cobre também aquecimento e linhas completas após a janela;
timestamps/contadores ausentes, negativos ou não inteiros invalidam a captura.
CPU ausente continua indisponível. Totais dos contadores usam inteiros de 64 bits
com verificação de overflow, sem arredondamento intermediário para double.

| Métrica | Definição e limite |
|---|---|
| FPS médio | intervalos / soma dos intervalos; swaps guest, não scanout |
| 1% low | inverso da média dos ceil(N×0,01) intervalos mais lentos |
| Frame time médio/P95/P99 | diferenças entre timestamps; percentil nearest-rank |
| CPU por frame | CPU agregada de todas as threads entre swaps; pode exceder parede |
| Uso de CPU | CPU agregada / parede / processadores lógicos × 100 |
| CPU externa P2 | TotalProcessorTime / janela de parede comum OFF/ON; ms de CPU/s e percentual da máquina |
| RAM | pico observado de working set e private commit em amostras de 1 s |
| Inicialização | Runtime::Setup até primeiro swap; não é gameplay pronto nem inclui criação do processo |
| Shader/pipeline | parede somada de tradução/criação D3D12, incluindo falhas/workers; não é duração GPU |
| Caches | textura/pipeline existentes; pipeline exclui fast path de estado idêntico consecutivo |
| Dispatch | FunctionDispatcher::Execute, não todas as chamadas AOT/indiretas |
| Swap failures | IssueSwap sem sucesso; cadência de tentativas não certifica imagem correta |
| GPU, VRAM, PMU, CPU critical path, gameplay ready | indisponíveis: JSON null com motivo, nunca zero inventado |

Eventos atômicos são resetados no snapshot. Tempos de workers entram no frame da
conclusão, não determinam a causa de um hitch. Traduções/criações antes do primeiro
swap entram na primeira linha, preservada no CSV mesmo se excluída da janela.
Não interpretar contadores sem callsites no backend como ausência de gargalo.
Tracy existente pode aprofundar investigações; não é necessário para o benchmark.

## Metodologia e execução

### Instrumentação OFF/ON em jogos reais (P2)

`benchmark-runtime.ps1 -Instrumentation Off|On` usa o mesmo executável e DLLs.
OFF passa `--perf_counters=false` e omite `perf_log_csv` (o parser CLI11 recusa
`--perf_log_csv=` vazio). Configurações TOML com override de CSV são recusadas
em OFF. ON habilita contadores, snapshots, timestamps de CPU e CSV bufferizado.
Não é comparação com remoção dos callsites em compile time. O runner exige
um log de runtime em até 10 s para detectar falha de inicialização/CLI.

CPU/RAM são coletadas externamente em **ambos** os modos, aproximadamente a
cada segundo. A janela usa o relógio monotônico do runner após Process.Start;
o custo agregado de CPU vem de `Process.TotalProcessorTime`, interpolado nas
duas bordas entre amostras. A interpolação assume taxa local aproximadamente
constante no intervalo de até ~1 s; não é CPU por frame ou caminho crítico.
São exigidas cobertura das bordas, CPU/timestamps monotônicos, RAM válida e
intervalos de amostragem de no máximo 2,5 s. Working set e private commit são
picos **amostrados**, não máximos absolutos. Uma CPU ocupada corresponde a
1000 ms de CPU/s; uso da máquina divide também pelos processadores lógicos.

O CSV nativo continua medindo frames somente em ON. A origem aproximada do
runtime (criação do CSV) alinha a janela de frames à janela externa; os intervalos
e percentis usam timestamps nativos. OFF tem FPS/1% low/P95/P99 `null`, com motivo:
não há coletor independente de frames validado. Assim, a comparação quantifica
CPU total e RAM da instrumentação, **não** seu efeito em FPS. GPU/VRAM também
continuam indisponíveis; identidade da placa/driver não é medição de uso.

```powershell
pwsh -NoProfile -File scripts/performance/benchmark-instrumentation.ps1 -Label p2-measured -Samples 4
# Ampliar a mesma série após examinar dispersão, sem sobrescrever as primeiras:
pwsh -NoProfile -File scripts/performance/benchmark-instrumentation.ps1 -Label p2-measured -Samples 8 -FirstSample 5
```

Quatro amostras usam OFF/ON/ON/OFF; oito acrescentam ON/OFF/OFF/ON. O driver
executa títulos sequencialmente, com watchdog de 330 s por filho; o runner limita
o jogo a menos de 300 s. Revisar `before.png`, `after.png`, eventos e logs de
cada coleta, registrar `scene_verified` e `scene_review` no ambiente e resultado,
e só então executar `compare-instrumentation.ps1 -Runs <diretórios-em-ordem>
-Output <json>`. O parâmetro `Runs` é um array PowerShell, passado por uma chamada
de script dentro de PowerShell, não como string contendo vírgulas em `pwsh -File`.

Esse comparador exige identidade de **todos** os binários, inclusive runtime,
originais, seeds, hardware, configuração, coletor e janela. Só admite variação
dos switches de instrumentação e caminhos de saída isolados. Reporta médias,
desvios, min/max e diferenças de CPU entre pares adjacentes. O IC95% t pareado
é exploratório: pressupõe diferenças aproximadamente normais e independentes;
2 ou 4 pares não certificam ausência de overhead nem determinismo da cena.
Frames individuais não são tratados como milhares de execuções independentes.
O redutor e o exportador preservam os dois modos e os hashes da evidência.

Os benchmarks são diagnósticos diretos com encerramento limitado forçado.
Não substituem nem alteram o ciclo original Dashboard → Forza → Dashboard.
Reusar a evidência funcional existente quando runtime/AOT/sessão não mudaram.

Usar PowerShell 7 e binários compilados por `scripts/devshell.cmd`, com SDK fixado
em `deps.lock.json`. Cenários: `benchmarks/scenarios.json`.

```powershell
pwsh -File scripts/performance/benchmark-runtime.ps1 -Title dash_17559 -Label baseline-1
pwsh -File scripts/performance/benchmark-runtime.ps1 -Title castle_crashers -Label baseline-1
pwsh -File scripts/performance/benchmark-runtime.ps1 -Title forza_horizon -Label baseline-1
```

Na retomada local de 2026-10-14, o prepare anterior do Forza foi interrompido
durante o build e seu diretório padrão está sem executável. Os artefatos
congelados, auditados e suficientes para o benchmark estão disponíveis:

```powershell
pwsh -File scripts/performance/benchmark-runtime.ps1 -Title forza_horizon -Label nova-coleta -BinaryDirectory .local/evidence/benchmarks/binaries/sdk72/forza_horizon
```

Usar `sdk71/forza_horizon` para o outro lado desse A/B. Não copiar executáveis
congelados para uma preparação incompleta nem emitir recibos a partir deles.

O runner recusa sobrescrita, sessões existentes e cenários acima de 300 s. Usa
ProcessStartInfo sem console, entrada/captura por scripts locais e PID exato,
deadline e cleanup em finally. Sem Computer Use. Capturas fora da janela medida.
PrintWindow roda em helper próprio com timeout de 5 s e cleanup, para que um
jogo sem resposta não bloqueie indefinidamente a coleta.
Uma seed imutável em `.local/evidence/benchmarks/seeds/<title>` alimenta cópias de perfis,
saves e caches por execução, preservando dados reais. A condição inicial de cache
é idêntica; cache do SO/driver não é controlado. Outra condição exige outro conjunto
explícito de seeds preservando o anterior. Não misturar originais entre títulos.

Saídas em `.local/evidence/benchmarks/<label>/<title>`: ambiente, CSV, resultado JSON,
amostras do processo, eventos, logs e PNGs. Registram hardware/driver/resolução,
power plan quando disponíveis, hashes de binários/DLLs/configuração, lock, cenário
e seed. É diagnóstico direto com guardas de originais do título; não publica
recibos nem comprova a biblioteca. Binários alterados exigem preparação normal
antes de uso em sessão certificada ou distribuição.

`-BinaryDirectory` permite congelar os mesmos AOT/DLLs por variante. Um
`provenance.json` opcional nessa pasta é incorporado ao ambiente: registrar a
origem real do build, inclusive alterações não commitadas. HEAD e hash do lock
registrados no lançamento identificam o checkout naquele momento; não comprovam
a origem de um executável congelado anteriormente.

O CSV nativo pode negar leitura concorrente no Windows; o runner só o lê depois
de encerrar o processo. O horário de criação do CSV aproxima a origem do runtime.
Nas capturas anteriores à P2, servia para agendar término e alinhar RAM ao runtime;
na P2, alinha a janela nativa ON ao relógio externo comum, sem controlar o prazo
ou a CPU/RAM externas. FPS e CPU por frame usam timestamps e CPU acumulada
nativos; os campos `external_*` usam as amostras do processo. Encerramento limitado
forçado é explicitado e não equivale a um retorno normal à dashboard.

```powershell
pwsh -File scripts/performance/compare-benchmarks.ps1 -Before .local/evidence/benchmarks/before/dash_17559 -After .local/evidence/benchmarks/after/dash_17559 -Output .local/evidence/benchmarks/delta.json
pwsh -File scripts/performance/export-benchmark.ps1 -Run .local/evidence/benchmarks/before/dash_17559 -Output .local/evidence/benchmarks/public.json
pwsh -File scripts/performance/reduce-benchmark.ps1 -Run .local/evidence/benchmarks/before/dash_17559
```

A comparação exige a mesma cena revisada, janela completa, seed, configuração,
hardware e AOT; permite explicitamente apenas as DLLs de runtime modificadas.
Exige também hashes dos coletores e dos módulos originais, recusando mudança na
implementação da medição. Conferir o diff do runtime continua necessário: a lista
de DLLs permitidas, por si só, não prova que apenas uma otimização mudou.
`reduce-benchmark.ps1` reprocessa evidência existente sem iniciar o título ou
alterar o CSV, preserva `result.initial.json` e registra hashes do CSV/redutor.
Reprocessar ambos os lados com a mesma versão antes de comparar; o comparador
recusa redutores divergentes. A procedência original da coleta permanece intacta.
Reduções repetidas preservam o erro de `result.initial.json`, inclusive após a
recuperação de uma cauda truncada. RAM sem amostras fica null com motivo. O
comparador recusa métricas obrigatórias ou identidade do executável ausentes,
além de erros explícitos de redução; dois registros incompletos iguais não
constituem evidência comparável.
Falhas de swap invalidam a comparação automática. Os erros de draw/resolve e
demais logs permanecem no resultado para revisão; contagens abrangem apenas os
arquivos retidos pela rotação. Exportação mantém métricas e hashes de evidências,
sem copiar perfis, saves, capturas ou caminhos de entrada para o documento público.
Execuções com entrada adicional durante calibração recebem `calibration_only=true`
no ambiente/resultado e são recusadas pelo comparador, mesmo com gameplay visível.
O cenário Forza confirma Start e Single Player antes da introdução; mede a primeira
cena dirigível após o carro parar, com HUD e tráfego ativos, sem entrada na janela.
O roteiro não é um replay determinístico: conferir também posição e estado nas
capturas antes de aceitar um par, sem atribuir variações de uma execução a ganho.

`x360n_perf_counter_probe` é um alvo explícito, fora dos benchmarks automáticos:
executa chamadas reais de `rex::perf`, valida totais e registra sete amostras de
um/quatro writers com contadores independentes, habilitados/desabilitados.
`scripts/performance/benchmark-counters.ps1 -Executable <probe.exe> -Output <resultado.json>`
limita a execução a 30 s. É uma carga sintética do runtime, não FPS de um jogo.

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Release --target x360n_perf_counter_probe
pwsh -File scripts/performance/benchmark-counters.ps1 -Executable .local/build/win-amd64/tests/Release/x360n_perf_counter_probe.exe -Output .local/evidence/benchmarks/counters.json
scripts/devshell.cmd ctest --test-dir .local/build/win-amd64 -C Release -R "x360n[.]benchmark"
```

O probe é `EXCLUDE_FROM_ALL`: compilado apenas quando solicitado. Testes CTest
dos scripts são sintéticos e não lançam jogos. Os testes SDK dos contadores usam
`sdkcheck.Performance` como filtro; não é necessário executar toda a suíte.

Conferir capturas e logs antes do aceite: `scene_verified` nasce false. Se a cena
não for a pedida, classificar o resultado como diagnóstico inválido para ela.
Registrar falhas de draw/resolve do Forza, nunca tratá-las como otimização.
Entrada temporizada não é replay determinístico: tráfego/simulação podem divergir.
Usar repetições alternadas A/B, mesmos AOT, perfis, configuração, hardware e carga
externa; registrar dispersão e não apenas o melhor resultado. Não executar jogos
de benchmark durante builds ou outros jogos. Testar apenas regressões pertinentes.

## Roadmap priorizado

1. Base implementada: referências, coleta opcional e patch 0072 contra false
   sharing dos contadores. Manter A/B de overhead e capturas de referência para
   cada mudança relevante; investigar caudas de frame antes de alegar ganho.
2. GPU timestamps D3D12 e memória DXGI, leitura atrasada sem introduzir esperas;
   distinguir GPU busy, pacing, fila e tempo CPU.
3. Medir fast path do pipeline, uploads/cópias, waits e PSOs; separar cache hit,
   tradução, criação no driver e startup.
4. Tracy focado nos hitches comprovados: locks, fibers, scheduling, alocações e
   chamadas runtime/AOT. Não remover sincronizações por especulação.
5. Avaliar LTO/PGO/inlining e qualidade do código nativo em builds isolados,
   preservando strict FP, aliasing e testes semânticos PPC.
6. Marcadores semânticos de carregamento/transição; caches persistentes somente
   com identidade/versionamento/invalidação comprovados.

Resultados: [2026-10-14](reports/performance/performance-results-2026-10-14.md).
P3: [encerramento por reversão da otimização 0073](reports/performance/performance-p3-2026-10-14.md).
Instrumentação e benchmarks preservados; a redução isolada de CPU não foi aceita
como ganho global diante da piora observada de 1% low.
