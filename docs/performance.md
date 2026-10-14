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

O runner recusa sobrescrita, sessões existentes e cenários acima de 300 s. Usa
ProcessStartInfo sem console, entrada/captura por scripts locais e PID exato,
deadline e cleanup em finally. Sem Computer Use. Capturas fora da janela medida.
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
```

A comparação exige a mesma cena revisada, janela completa, seed, configuração,
hardware e AOT; permite explicitamente apenas as DLLs de runtime modificadas.
Falhas de swap invalidam a comparação automática. Os erros de draw/resolve e
demais logs permanecem no resultado para revisão; contagens abrangem apenas os
arquivos retidos pela rotação. Exportação mantém métricas e hashes de evidências,
sem copiar perfis, saves, capturas ou caminhos de entrada para o documento público.

`x360n_perf_counter_probe` é um alvo explícito, fora dos benchmarks automáticos:
executa chamadas reais de `rex::perf`, valida totais e registra sete amostras de
um/quatro writers com contadores independentes, habilitados/desabilitados.
`scripts/benchmark-counters.ps1 -Executable <probe.exe> -Output <resultado.json>`
limita a execução a 30 s. É uma carga sintética do runtime, não FPS de um jogo.

Conferir capturas e logs antes do aceite: `scene_verified` nasce false. Se a cena
não for a pedida, classificar o resultado como diagnóstico inválido para ela.
Registrar falhas de draw/resolve do Forza, nunca tratá-las como otimização.
Entrada temporizada não é replay determinístico: tráfego/simulação podem divergir.
Usar repetições alternadas A/B, mesmos AOT, perfis, configuração, hardware e carga
externa; registrar dispersão e não apenas o melhor resultado. Não executar jogos
de benchmark durante builds ou outros jogos. Testar apenas regressões pertinentes.

## Roadmap priorizado

1. Estabelecer referências, validar coleta/overhead e medir custo de contadores e
   caches com concorrência sintética e cenários reais.
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
