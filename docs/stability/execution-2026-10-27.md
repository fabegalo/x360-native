# STABLE-01 — execução de 27/10/2026

**Certificação recusada.** Baseline main `9abb880` / Preview 25, Windows x64,
Release, SDK `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, 201 patches ativos.
Resultados individuais, commits completos, hash agregado dos arquivos SDK,
hash do lock e referências de evidência: [matrix.json](matrix.json).
Critérios executáveis: [criteria.json](criteria.json).
O hash agregado SDK usa SHA-256 do JSON compacto dos pares ordenados
`[caminho relativo, SHA-256]` dos 271 arquivos finais dos patches; o commit
base, os pins dos submódulos e suas alterações locais são verificados à parte.

| Verificação | Resultado |
|---|---|
| Pin, patches e hashes finais SDK; alterações rastreadas fora do lock e pins dos submódulos | PASS |
| Build dos targets Release atuais da main e da candidata isolada | PASS |
| 37 contratos obrigatórios da main | 36 PASS, 1 FAIL preservado |
| Catálogo/importação sintéticos, na candidata | PASS |
| Atualização e recuperação após morte do processo, na candidata | PASS; inclui bytes binários de perfil e saves de dois usuários/dois títulos no HDD único |
| Crash → relançamento explícito → dashboard/jogo/retorno → fechamento → relançamento | PASS sintético na candidata; não é save/load comercial |
| Regressão focada da alocação de diretórios, sessão e preferências | 5 casos, 1.011 verificações PASS, 51,834 s |
| Controles adversariais do gate | 23 testes PASS |
| Ciclo original completo; preparo pelo aplicativo; save/load comercial; Windows limpo | 4 NOT RUN obrigatórios; bloqueiam certificação |

Comando da execução final: `stability_gate.py run`, com os caminhos main/build
e desktop isolado definidos no [README](README.md), saída
`.local/evidence/stable-01/run-08/`, usando
`--reuse-contracts .local/evidence/stable-01/run-04/report.json` e
`--reuse-candidate .local/evidence/stable-01/run-07/report.json`.
O gate verificou identidade, conjunto e bytes dos artefatos main/SDK e XML
antes de reaproveitar os 37 contratos. Preservou a falha, sem repetir os 36
aprovados. Desktop e cenário novo executaram sobre os binários da candidata
em `run-07`; `run-08` verificou todos os mesmos artefatos antes de reutilizá-los,
executando os 23 controles novos e ampliando a checagem do SDK/submódulos.
`run-08/report.json` contém comandos, timeouts, hashes de EXE/DLL/fontes SDK,
logs e XML. Saída **1** é o resultado correto enquanto existem bloqueios.
A reverificação independente do recibo final retornou 1 somente pelo FAIL
da baseline e pelos quatro NOT RUN, sem erro de integridade/evidência.

## Falha da main e correção entregue

`x360n.console preferences survive title handoffs and reload on session restart`
falhou na segunda sessão, com retorno 3. A versão inicialmente adicionada do
cenário de recuperação também alternou PASS e retorno 3. A inspeção encontrou
o caminho: `Run` usava timestamp em segundos + label, reutilizava diretório
existente e recusava seu `handoff.bin` antigo. Não havia mensagem nessa recusa.

`c788f9d` passa a reservar um diretório novo com criação atômica e tentativas
limitadas. A recusa de handoff existente permanece. O teste força colisões com
timestamp fixo, preserva os bytes do handoff anterior, verifica erro de diretório
bloqueado e percorre crash/relançamentos com hashes do HDD intactos. Os cinco
casos relacionados passaram na candidata, incluindo preferências e 80 transições.
A falha original da main continua FAIL na matriz; a correção local não transforma
um resultado passado da main em sucesso.

## Tentativas e falhas preservadas

Todos os registros ficam em `.local/evidence/stable-01/`; nenhum contém aceite
de jogo real. Não se descartaram falhas para selecionar uma execução verde.

- Build inicial isolado: faltava `-march=x86-64-v2`; falhou antes dos testes.
  Configuração corrigida para as flags do projeto; SDK e oráculos preservados.
- `run-01` a `run-03`: preflight recusado por diferença de fim de linha entre
  checkouts do lock e ambiente de módulos PowerShell. Agora a comparação entre
  checkouts usa JSON equivalente, mantendo hashes exatos de cada arquivo;
  os comandos usam os módulos Windows explícitos. Nenhum teste em binário velho.
- `run-04`: 36/37 main PASS; preferências FAIL. Catálogo isolado: cinco falhas
  por template do SDK buscado no caminho fixo errado. O fixture passou a usar
  `X360_REXGLUE_DIR`, sem mudar dados esperados. O cenário novo inicialmente
  comparava CRLF com LF: passou a conferir exatamente os seis registros
  numéricos do protocolo, mantendo as verificações de integridade.
- `run-05`: o controle novo de timeout revelou tratamento incorreto de chave
  Windows `SystemRoot`. Corrigido; o processo sintético foi encerrado, e o
  controle agora demonstra encerramento limitado da árvore de processos.
- `run-06`: catálogo e update PASS; falha intermitente de relançamento expôs a
  colisão de diretórios descrita acima. Mantida como falha nessa execução.

Controles negativos cobrem: todos os estados não PASS, resultado obrigatório
omitido/duplicado, JUnit vazio/duplicado ou divergente, evidência ausente,
artefato omitido/trocado, dependência não inventariada, save truncado,
identidade de conteúdo/SDK/lock divergente, backend errado, build falho,
reuso com identidade/artefatos divergentes, fonte SDK alterada fora do lock e
arquivo de patch com bytes incorretos. Falha histórica reaproveitada continua FAIL.
Um submódulo com commit correto e arquivo local adulterado também é recusado;
as únicas materializações libmspack aceitas precisam corresponder ao blob original.
Corrupção aqui é detectada por identidade/integridade de evidência; não se
implementou um validador universal dos formatos de saves comerciais.

## P0 e sequência objetiva

1. Integrar/revisar a correção `c788f9d`; então repetir o caso de relançamento
   na main alterada. A baseline atual permanece bloqueada pelo seu FAIL.
2. Renovar o preparo dos produtos necessários e certificar o ciclo original
   17559/perfis/My Games/Guide/entrada/áudio/retorno no lock atual, incluindo
   fidelidade Xenos e classificação das diferenças gráficas aplicáveis.
3. Selecionar jogo/licença com salvamento real e obter autorização específica
   do ensaio. Executar save → retorno → fechamento → relançamento → load,
   reiniciar plataforma, confirmar progresso e isolamento/preservação da origem.
4. Validar importação/preparação interrompida e recuperação pelo aplicativo,
   e distribuição/atualização no Windows limpo, conforme [pré-requisitos](README.md).

As 45 diferenças Xenos R8/API, quatro probes Native com falhas históricas e
falha upstream Depfile permanecem nas evidências anteriores; não são PASS nem
foram reexecutados nesta etapa. Native/Vulkan seguem experimentais, sem mudança
de renderer/SDK ou reivindicação de estabilidade. Não houve suite completa,
sessão visual, Computer Use, execução de conteúdo original, alteração de
licenças/oráculos gráficos, push, tag ou Release. Nenhuma medição de gameplay
ou alegação de ganho de performance. Originais, perfis/saves, histórico, patches,
SDK pinado, arquivos locais preexistentes e main foram preservados.
