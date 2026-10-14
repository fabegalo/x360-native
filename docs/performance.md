# Performance-first, correctness-preserving

Performance é objetivo central do x360-native. A recompilação estática PowerPC
para x64 permite investir em AOT e runtime compartilhado, preservando os originais.
Toda melhoria exige medição antes/depois. Não reduzir fidelidade, omitir erros,
misturar módulos de versões, alterar semântica PPC/XAM, retirar sincronizações
necessárias ou usar stubs para obter números melhores. Preservar C7/C8, perfis,
saves e biblioteca. Compilador, AOT, CPU, memória, GPU e carregamento são frentes
contínuas, priorizadas por evidência.

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

Usar PowerShell 7 e binários compilados por `scripts/devshell.cmd`, com SDK fixado
em `deps.lock.json`. Cenários: `benchmarks/scenarios.json`.

```powershell
pwsh -File scripts/benchmark-runtime.ps1 -Title dash_17559 -Label baseline-1
pwsh -File scripts/benchmark-runtime.ps1 -Title castle_crashers -Label baseline-1
pwsh -File scripts/benchmark-runtime.ps1 -Title forza_horizon -Label baseline-1
```

Na retomada local de 2026-10-14, o prepare anterior do Forza foi interrompido
durante o build e seu diretório padrão está sem executável. Os artefatos
congelados, auditados e suficientes para o benchmark estão disponíveis:

```powershell
pwsh -File scripts/benchmark-runtime.ps1 -Title forza_horizon -Label nova-coleta -BinaryDirectory .local/benchmarks/binaries/sdk72/forza_horizon
```

Usar `sdk71/forza_horizon` para o outro lado desse A/B. Não copiar executáveis
congelados para uma preparação incompleta nem emitir recibos a partir deles.

O runner recusa sobrescrita, sessões existentes e cenários acima de 300 s. Usa
ProcessStartInfo sem console, entrada/captura por scripts locais e PID exato,
deadline e cleanup em finally. Sem Computer Use. Capturas fora da janela medida.
PrintWindow roda em helper próprio com timeout de 5 s e cleanup, para que um
jogo sem resposta não bloqueie indefinidamente a coleta.
Uma seed imutável em `.local/benchmarks/seeds/<title>` alimenta cópias de perfis,
saves e caches por execução, preservando dados reais. A condição inicial de cache
é idêntica; cache do SO/driver não é controlado. Outra condição exige outro conjunto
explícito de seeds preservando o anterior. Não misturar originais entre títulos.

Saídas em `.local/benchmarks/<label>/<title>`: ambiente, CSV, resultado JSON,
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
de encerrar o processo. O horário de criação do CSV aproxima a origem do runtime
exclusivamente para agendar término e amostras externas de RAM. Os cálculos de
FPS/CPU usam somente timestamps e CPU acumulada nativos. Encerramento limitado
forçado é explicitado e não equivale a um retorno normal à dashboard.

```powershell
pwsh -File scripts/compare-benchmarks.ps1 -Before .local/benchmarks/before/dash_17559 -After .local/benchmarks/after/dash_17559 -Output .local/benchmarks/delta.json
pwsh -File scripts/export-benchmark.ps1 -Run .local/benchmarks/before/dash_17559 -Output .local/benchmarks/public.json
pwsh -File scripts/reduce-benchmark.ps1 -Run .local/benchmarks/before/dash_17559
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
`scripts/benchmark-counters.ps1 -Executable <probe.exe> -Output <resultado.json>`
limita a execução a 30 s. É uma carga sintética do runtime, não FPS de um jogo.

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Release --target x360n_perf_counter_probe
pwsh -File scripts/benchmark-counters.ps1 -Executable .local/build/win-amd64/tests/Release/x360n_perf_counter_probe.exe -Output .local/benchmarks/counters.json
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

Resultados: [2026-10-14](performance-results-2026-10-14.md).
