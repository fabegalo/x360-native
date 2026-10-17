# P2 — instrumentação OFF/ON nos jogos reais

## Escopo e procedência

Continuação do HEAD local `7148a727046b60ea7ef4760cb5a55dfbee5acc98`, preservando
`f867e04`. Reutilizados os executáveis AOT e todas as DLLs congeladas em
`.local/benchmarks/binaries/sdk72/{castle_crashers,forza_horizon}`. Nenhum título
ou runtime recompilado. Runtime medido:
`a1159404a7aac4c5ff0c0ff6ffd3dcea9f2cf72391fc1d21e2161c3088baa96f`.
O `provenance.json` anterior registra fonte dirty e origem dos builds; HEAD do
lançamento identifica o checkout, não transforma o build congelado em build limpo.
SDK permanece em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os 72 patches
existentes. Não houve edição do submódulo nesta tarefa.
Implementação do coletor/redutor/comparador desta rodada: commit local `389fe31`.
As execuções ocorreram antes desse commit e registram corretamente `7148a72`
como HEAD do lançamento, com hashes dos scripts modificados.

Títulos exatos: Castle Crashers `584108B7 / 0.0.1.3` e Forza Horizon
`4D5309C9 / 2DC7007B / 0.0.0.10`, conforme os perfis de preparação e hashes
dos XEX. Nenhum módulo de dashboard de outra versão foi usado como substituto.

Ryzen 7 5700X3D (8 cores/16 lógicos), RTX 4070, driver 32.0.16.1742,
42.856.640.512 bytes de RAM, Windows NT 10.0.26300.0. Display 1920×1080/279 Hz,
plano Equilibrado. Xenos/D3D12, opções gráficas originais dos binários, vsync
padrão, sem redução de fidelidade. Todos os hashes de executáveis, DLLs,
originais, seeds, coletores, lock e evidências estão nos JSON exportados.

## Método e limites

- Cada execução copia a mesma seed imutável de perfil/save/cache. Os dados reais
  do usuário não são destino de gravação. Cache do SO/driver não controlado.
- Castle: menu principal animado com Local Game selecionado; aquecimento 75 s,
  janela de 30 s. É execução real do jogo, **não benchmark de combate/gameplay**.
- Forza: roteiro existente de Start/Single Player e primeira cena dirigível;
  aquecimento 240 s, janela de 30 s, sem entrada nessa janela. Não é replay
  determinístico; posição/HUD/cena são conferidos em capturas antes/depois.
- OFF/ON/ON/OFF, com extensão ON/OFF/OFF/ON quando a dispersão inicial justifica.
  Comparação exige todos os binários idênticos, inclusive DLL de runtime.
- ON inclui todos os contadores existentes, snapshots, timestamps/CPU por frame
  e CSV bufferizado. OFF desliga ambos os switches. O teste mede o custo agregado
  desse conjunto; não isola contadores e não mede remoção dos callsites por build.
- CPU é medida externamente pelo processo em ambos os modos, em ms de CPU/s e
  percentual dos 16 processadores lógicos. Bordas interpoladas em amostras de
  ~1 s. RAM/private commit são picos amostrados. O coletor externo é comum aos
  dois lados, e seu próprio processo não entra na CPU do jogo.
- FPS, 1% low e P95/P99 de ON são cadência de swaps guest, não scanout. **OFF não
  tem fonte independente de frames validada**; esses campos e o delta de FPS
  ficam `null`. Não se atribui ganho de FPS aos contadores. GPU/VRAM também são
  `null`; consulta de identidade da placa não equivale a medição de uso.
- Capturas por script fora da janela; nenhum Computer Use. Processos sequenciais,
  limitados e acompanhados. Encerramento forçado registrado, sem afirmar retorno
  normal da coleta ao dashboard. O ciclo aprovado Dashboard → Forza → Dashboard
  permanece inalterado, com evidência anterior em `validation-forza-horizon.md`.

Estatísticas agrupam **execuções**, não cada frame como amostra independente.
Diferenças pareadas usam runs adjacentes; IC95% t é exploratório, com hipótese de
diferenças aproximadamente normais/independentes e poucos graus de liberdade.
Expandir a amostra após inspecionar resultados não cria um teste confirmatório.
Ausência de diferença resolvida não significa custo zero.

Foi avaliado PresentMon como fonte externa para OFF. A consulta do token local
retornou `ElevatedAdministrator=false` e `PerformanceLogUsers=false`. A
[documentação oficial do PresentMon](https://github.com/GameTechDev/PresentMon#user-access-denied)
exige acesso ao ETW pelo grupo Performance Log Users (ou execução elevada).
Não foram instalados serviços nem alteradas permissões/grupos da máquina.
Logo, o delta de CPU aqui é consumo por tempo de parede na cena equivalente;
sem frames OFF, não comprova custo por quadro nem igualdade exata de throughput.
Essa lacuna fica explícita para a próxima rodada com coletor externo habilitado.

### Primeiro bloco e extensão

Os primeiros quatro runs de cada jogo passaram no comparador e na revisão de
cena. Castle deu −4,16% de CPU ON/OFF, IC95% exploratório equivalente a
−16,24%…+7,93% da média OFF. Forza deu −0,69%, com pares de +0,92% e −2,28%;
IC equivalente a −21,11%…+19,73%. São apenas dois pares/um grau de liberdade;
esses intervalos amplos não sustentam alegação de ganho nem de custo zero.

Por isso a rodada foi estendida, com limite definido de oito runs por jogo,
acrescentando ON/OFF/OFF/ON. Não se repetiu até aparecer significância. Os
primeiros blocos permanecem disponíveis:
[Castle, quatro runs](../benchmarks/results/2026-10-14/p2/castle_crashers-first-four.json)
e [Forza, quatro runs](../benchmarks/results/2026-10-14/p2/forza_horizon-first-four.json).

## Resultados das oito amostras

Média ± desvio padrão entre quatro execuções por modo. Δ é
`100 × (média ON / média OFF − 1)`. O IC usa as quatro diferenças entre pares
adjacentes, em ms de CPU/s, sem tratar frames como observações independentes.

| Cenário | CPU OFF (ms/s) | CPU ON (ms/s) | Δ CPU | IC95% da diferença (ms/s) |
|---|---:|---:|---:|---:|
| Castle, menu | 1.032,72 ± 34,49 | 1.008,78 ± 11,46 | −2,32% | −63,32 a +15,42 |
| Forza, carro parado/tráfego ativo | 2.393,67 ± 54,63 | 2.388,37 ± 58,85 | −0,22% | −71,78 a +61,17 |

Castle: a diferença não se separa do ruído nesta rodada. Todos os quatro pares
tiveram CPU menor em ON, porém o efeito caiu de −3,22%/−5,08% no primeiro bloco
para −0,31%/−0,44% no segundo. Não atribuir isso a ganho ou overhead negativo;
faltam frames OFF e controle de estado/caches/scheduling para tal conclusão.
Uso médio da máquina: 6,455% OFF e 6,305% ON.

Forza: os quatro pares variaram +0,92%, −2,28%, +1,46% e −0,97%, sem direção
consistente. Uso médio da máquina: 14,960% OFF e 14,927% ON. A queda de CPU em
ambos os modos no último par mostra por que uma amostra isolada seria enganosa.
O custo agregado de ativar contadores/snapshots/timestamps/CSV não foi resolvido
pela precisão desta rodada. Os ICs, expressos em relação à média OFF, são
−6,13%…+1,49% para Castle e −3,00%…+2,56% para Forza. São intervalos
exploratórios de consumo observado, **não limites garantidos de overhead causal**.

Médias dos picos amostrados por execução, em MiB (2²⁰ bytes):

| Cenário | Working set OFF | Working set ON | Private commit OFF | Private commit ON |
|---|---:|---:|---:|---:|
| Castle, menu | 458,05 | 458,34 | 895,47 | 894,64 |
| Forza, carro parado/tráfego ativo | 1.177,91 | 1.179,64 | 2.476,75 | 2.477,68 |

Frames **somente ON**; médias das métricas calculadas em cada execução, não
percentis de um pool artificial de runs. OFF e os deltas de FPS são indisponíveis.

| Cenário | FPS médio | 1% low | Frame médio (ms) | P95 (ms) | P99 (ms) |
|---|---:|---:|---:|---:|---:|
| Castle, menu | 60,000 | 50,275 | 16,667 | 18,245 | 19,327 |
| Forza, carro parado/tráfego ativo | 29,982 | 26,662 | 33,353 | 35,519 | 36,041 |

Castle: oito cenas revisadas, zero erros/críticas e três avisos de inicialização
em cada run; zero swaps falhos nas quatro coletas ON.
[Comparação completa de Castle](../benchmarks/results/2026-10-14/p2/castle_crashers-comparison.json).

Forza: oito cenas revisadas; zero críticas e zero swaps falhos nas quatro ON.
Persistem 175.841–176.345 erros e 94.230–94.559 avisos nos logs retidos por run,
principalmente draw/resolve/fetch já conhecidos. Essas contagens incluem warmup
retido e dependem da rotação; não são taxas da janela medida. Não foram ocultadas
nem classificadas como ganho.
[Comparação completa de Forza](../benchmarks/results/2026-10-14/p2/forza_horizon-comparison.json).

### Auditoria e dados individuais

As 16 janelas completas passaram: hashes de evidências/exportações e coletores
conferem, todas as entradas terminaram no aquecimento, nenhum CSV em OFF e nenhum
swap falho em ON. CPU externa e nativa ON diferem menos de 0,17% em cada run,
checagem de consistência útil apesar do alinhamento nativo aproximado. A maior
lacuna de amostragem externa ficou abaixo de 1,07 s. Nenhum jogo permaneceu aberto
após a última coleta. Evidências locais:
`.local/logs/p2-final-evidence-audit.json`, `p2-final-process-check.json` e
`p2-etw-permissions.json`.

CPU total, em ms de CPU/s; cada célula é uma execução de 30 s após aquecimento:

| Amostra/modo | Castle | Forza |
|---|---:|---:|
| 01 OFF | 1.056,70 | 2.402,35 |
| 02 ON | 1.022,66 | 2.424,54 |
| 03 ON | 1.013,70 | 2.401,34 |
| 04 OFF | 1.067,94 | 2.457,26 |
| 05 ON | 999,27 | 2.425,87 |
| 06 OFF | 1.002,40 | 2.390,88 |
| 07 OFF | 1.003,86 | 2.324,20 |
| 08 ON | 999,49 | 2.301,72 |

[Manifesto JSON](../benchmarks/results/2026-10-14/p2/dataset.json) identifica
títulos/versões, executáveis, ordem, amostra excluída, hashes das 16 exportações,
comparações e testes. Dados brutos em
`.local/benchmarks/p2-measured-<NN>-<off|on>/<title>`; a exportação não distribui
originais, capturas, perfis ou saves. Foram 480 s de janelas medidas, além dos
aquecimentos e da tentativa inválida preservada.

## Correções e validação da infraestrutura

1. O runner anterior sempre habilitava CSV e dependia dele para delimitar a
   coleta. Agora aceita OFF verdadeiro, mantém o prazo sem CSV e usa o mesmo
   coletor de CPU/RAM nos dois modos. Redução posterior preserva esse relógio.
2. Na primeira tentativa de implementação, `--perf_log_csv=` foi recusado pelo
   CLI11, deixando janela preta. Run `p2-abba-01-off/castle_crashers` preservado
   e marcado inválido, com stderr e resultado inicial. Corrigido para omitir a
   opção e recusar overrides de CSV no TOML. Guarda de log em 10 s detecta esse
   tipo de falha sem gastar toda a janela. A série aceita começa em `p2-measured`.
3. Novo comparador recusa misturas de modos, ordem incorreta, cenas não revistas,
   mudanças em qualquer binário, originais, seeds, configuração ou coletor,
   redutores diferentes, janelas incompletas e valores ausentes/não finitos.
   Não calcula deltas sobre evidência rejeitada nem inventa FPS OFF.
4. Redutor e exportador preservam modo, relógio, origem aproximada do runtime e
   hashes dos samples externos. JSON público exclui caminhos privados, saves,
   perfis e capturas; os dados brutos permanecem em `.local/benchmarks`.

Testes focados: resumo de processo (interpolação, normalização e sete rejeições),
comparação de instrumentação (ABBA, incerteza e doze rejeições), resumo nativo,
comparador anterior, redução/redução repetida ON/OFF e exportação sanitizada.
Evidência local: `.local/logs/p2-focused-script-tests.log`. Os dois testes nativos
dos contadores/15 asserções do mesmo runtime já aprovados foram reutilizados;
não houve regressão completa nem nova suíte PPC/C7/C8 sem alteração relevante.

## Reprodução

```powershell
pwsh -NoProfile -File scripts/performance/benchmark-instrumentation.ps1 -Label nova-serie -Samples 4
# Após completar/revisar a primeira série, extensão explícita:
pwsh -NoProfile -File scripts/performance/benchmark-instrumentation.ps1 -Label nova-serie -Samples 8 -FirstSample 5
```

Os comandos iniciam jogos reais e exigem sessão exclusiva. O watchdog é de 330 s
por filho; cada jogo termina em menos de 300 s. Para comparar, passar ao
`compare-instrumentation.ps1` os diretórios de uma série em ordem cronológica,
depois de registrar a revisão das duas capturas e logs de cada run.
`reduce-benchmark.ps1` reprocessa sem iniciar jogos; `export-benchmark.ps1`
exporta métricas/procedência sem distribuir conteúdo original.

Exemplo de reprocessamento/comparação da série já coletada, sem iniciar jogos:

```powershell
$order = '01-off','02-on','03-on','04-off','05-on','06-off','07-off','08-on'
foreach ($title in 'castle_crashers','forza_horizon') {
  $runs = @($order | ForEach-Object { ".local/benchmarks/p2-measured-$_/$title" })
  foreach ($run in $runs) { & scripts/performance/reduce-benchmark.ps1 -Run $run }
  & scripts/performance/compare-instrumentation.ps1 -Runs $runs -Output ".local/benchmarks/$title-p2-comparison.json"
}
```

Em uma série nova, revisar `before.png`, `after.png`, eventos e logs antes de
marcar `scene_verified=true` e descrever `scene_review` em environment/result.
O comparador recusa amostras ainda não revistas. Não copiar essa aprovação
das amostras antigas para uma execução futura.

A revisão do código confirmou que `IssueSwap` ocorre antes do bloco opcional
de coleta; os leitores dos snapshots no runtime ficam no overlay de diagnóstico.
Não foi alterada decisão PPC/XAM/renderização para alternar o modo. Isso,
junto às cenas/logs das coletas, valida o alcance observado, sem prometer
equivalência de toda a campanha ou execução bit a bit.

## O que esta baseline permite investigar em seguida

- A primeira lacuna de medição é um provedor independente de frames OFF, com
  permissões ETW apropriadas. Sem ele, nenhuma diferença de CPU deve ser
  promovida a ganho de FPS ou a custo por quadro.
- Forza apresenta alto volume de trabalho no processador de comandos e erros
  preexistentes de draw/resolve/fetch. Os contadores registram trabalho emitido,
  não tempo GPU nem imagem correta. Priorizar diagnóstico do caminho de draw,
  logging e esperas com medição focada; não ocultar os erros ou pular operações.
  Nas quatro janelas ON houve 2,53–2,60 milhões de draws e 79,73–83,17 milhões de
  dispatches por 30 s. Traduções de shader e criações de pipeline foram zero
  nessas janelas; a atividade de startup está registrada separadamente. Portanto,
  esta baseline não aponta compilação de shader/PSO como gargalo da cena aquecida.
- O volume de dispatches torna esse caminho candidato a perfil de CPU, mas o
  contador de chamadas não demonstra que ele seja o caminho crítico. Diferenciar
  execução útil, waits e scheduling antes de alterar o runtime.
- Os percentis têm mais variação entre runs que o FPS médio limitado pelo jogo.
  Regressões futuras precisam repetir a mesma série/seed e comparar dispersão;
  uma melhoria isolada de 1% low não basta para declarar uma otimização.

Esta rodada não compara SDK0071 contra SDK0072. Os dois lados usam SDK0072;
portanto, não transforma a redução sintética de 94,59% dos quatro contadores
em ganho comercial de FPS. Nenhuma otimização especulativa foi implementada.
