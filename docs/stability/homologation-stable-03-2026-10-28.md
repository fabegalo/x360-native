# STABLE-03 — homologação manual e candidata v1.0.0-rc.1

Em 28/10/2026 o mantenedor confirmou os quatro aceites: dashboard → My Games →
jogo → Guide → dashboard; salvar → fechar tudo → reabrir → carregar; importação
e preparação pela interface; instalação em Windows limpo, atualização e
preservação dos dados. Indicou `Desktop/x360-release` como ambiente correto.
**Os quatro resultados estão aceitos por relato do mantenedor.** Não se exige
repeti-los nesta tarefa. Nenhum teste visual ou gameplay novo foi executado.

## Identidade recuperada

O diretório indicado contém `x360-native-0.69.27-preview.25-windows-x64.zip`,
11.210.133 bytes, SHA-256
`8ae222d09374e010bca9055284a966fd824b79d4923a5eeca9fc3bcd54b257c3`.
`package.json` identifica commit `9abb880096d3433f04b991ef5491c8b4a80c763c`,
Release/Windows x64, LLVM 22.1.8, ReXGlue
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` e 201 patches.
Procedência declarada: Actions `40555738624`, imagem `win22/20261022.344.1`,
MSVC `14.44.35207`, Windows SDK `10.0.26100.0`. Isso identifica o **build**;
não comprova versão do Windows/CPU/driver da máquina do ensaio limpo.

CRC e índice `package.sha256`: **1.178 arquivos conferidos** no ZIP e na
instalação indicada, zero divergências. Nenhum conteúdo dessa instalação foi
alterado. Inventário privado com caminhos, hashes, logs, importação e recibos:
`.local/evidence/stable-03/recovered-evidence.json`; não publicar perfis/saves.
Resumo público: [evidências recuperadas](recovered-stable-03.json).

Foram encontrados apenas estes dois produtos preparados nesse ambiente:

| Alvo | Versão e backend documentados | Evidência e alcance |
|---|---|---|
| Retail 17559 | `2.0.17559.0`, Xenos/D3D12 | Recibo íntegro, 4.468 referências inputs/artefatos/produtor conferidas; módulos e recursos da própria 17559 |
| Forza Horizon | `0.0.0.10`, Title ID `4D5309C9`, Xenos/D3D12 | Recibo íntegro, 4.534 referências conferidas; XEX `db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c` |

Receitas: `data/environment/titles/{dash_17559,forza_horizon}/preparation.toml`,
respectivos manifests, inventários e recibos, todos identificados no inventário.
O payload instalado confere com o ZIP. Os relatórios de execução não incluem
commit do projeto (`project_commit: null`): a procedência do desktop é comprovada
pelo pacote; não se inventa um commit independente para cada produto AOT.
Hashes dos EXEs/DLLs e produtores efetivos estão nos recibos recuperados.

Os logs registram NVIDIA GeForce RTX 4070 e argumentos `gpu_plugin=xenos`,
`gpu_backend=d3d12`, dashboard `2.0.17559.0`, HDD/perfis da dashboard repassados
ao Forza. Em 27/10, 23:35–23:51 de São Paulo (28/10 UTC), há sequência
`20261028T023558Z-session-0-dash_17559` → Forza → dashboard → Forza → dashboard
→ Forza e novo supervisor `20261028T025104Z-session-0-dash_17559` → Forza.
Há encerramentos por usuário com código 1; não são convertidos em PASS do
processo. Logs contêm stubs/erros de serviços: não são apresentados como limpos.

## Matriz de homologação

| Critério | Resultado relatado | Documentação recuperada | Automação |
|---|---|---|---|
| Ciclo original | ACEITO pelo mantenedor | Handoffs, relatórios, recibos, hashes, backend e sequência dashboard/jogo/retorno | Gate de contratos separado |
| Salvar/fechar/reabrir/carregar | ACEITO pelo mantenedor | Relançamentos e arquivos do ambiente; marco de progresso e comparação antes/depois não registrados independentemente | C8 e recuperação sintética não demonstram progresso comercial |
| Importação/preparação GUI | ACEITO pelo mantenedor | Catálogo, registros de importação e dois recibos íntegros | Catálogo/publicação/recuperação sintéticos separados |
| Windows limpo/atualização/dados | ACEITO pelo mantenedor | ZIP e instalação íntegros; faltam inventário histórico da máquina limpa e hashes antes/depois da atualização | Dependências e atualização sintética do RC têm recibos próprios |

O conjunto **relatado e documentado** é Windows x64, Retail 17559 e Forza Horizon
0.0.0.10 em Xenos/D3D12, restrito aos quatro fluxos confirmados. Não certifica
campanha inteira, FPS, outros jogos/dashboards nem compatibilidade universal.
Native e Vulkan permanecem experimentais. Ausência de metadados históricos é
limite da rastreabilidade, não anulação do aceite humano nem PASS automático.

## MAINT-01 — revisão e integração

A branch `codex/maint-01` ainda apontava para `6501e0e`, com alterações locais.
Foram revisados e trazidos para esta candidata os oito arquivos da manutenção:
wrapper/inventário Python, empacotador/limpeza de intermediários, CMake desktop
e três arquivos de testes. A worktree de origem foi preservada.

- O inventário não migra ou deduplica conteúdo. A aplicação restringe-se a
  intermediários declarados pelo CMake/Ninja; revalida identidade/hash, protege
  hardlinks, junctions, árvores com recibos, storage escolhido e processos ativos.
- Builds de desenvolvimento mantêm supervisor/extrator adjacentes ao launcher;
  distribuição mantém dependências e copia os targets originais para o ZIP.
- Limpeza do pacote só ocorre após sucesso e recibo; EXEs/DLLs, fontes, logs,
  recibos e índices são preservados. O RC usa `-KeepIntermediates` para reter
  toda a evidência de compilação. Não se executou limpeza no acervo real.
- 14 testes de manutenção PASS, teste de limpeza privada PASS e configuração
  real CMake ON/OFF PASS. A primeira execução teve bloqueios do sandbox para
  hardlink/junction; os 14 passaram com permissão adequada, sem enfraquecimento.
  O processo PowerShell de staging ficou preso após imprimir PASS e sua árvore
  foi encerrada; o configure comprovado não é tratado como teste de lançamento.
- Tooling da Release: 18 testes PASS; versionamento, seleção de startup e
  configuração Release PASS. Primeiras tentativas bloqueadas pelo sandbox em
  diretórios temporários foram preservadas nos logs. Publicação nesses testes
  é simulada; nenhum release remoto foi criado.

## Identidade final e verificação da candidata

A decisão explícita do mantenedor de preparar a v1.0.0 é registrada no footer
`Release-As: 1.0.0` do commit desta candidata. `compute-version.ps1` deve retornar
`1.0.0`; o sufixo `-rc.1` segue a validação existente de tags. Sem alteração do
algoritmo, do SDK pinado, da cadeia de 201 patches ou dos arquivos originais.

O gate completo solicitado deve executar sobre o HEAD final, sem `--reuse-*`,
em `.local/evidence/stable-03/final-gate/`. O recibo identifica o commit final,
EXEs/DLLs, SDK, JUnit, tempos e bloqueios; não herda a certificação de `9abb880`.
O runner sempre começa os quatro critérios manuais como NOT RUN. O relato
da Preview 25 permanece na matriz acima; não será reescrito como observação
do RC em identidade diferente para forçar saída 0 do verificador.

Pacote local previsto: `.local/packages/stable-03/` (ZIP, SHA-256 e pasta).
Extração de verificação isolada, dependências e startup não visual ficam em
`.local/evidence/stable-03/`; atualização/recuperação automatizadas não são um
novo ensaio humano de Windows limpo. Resultado final e SHA do ZIP ficam no
recibo local `delivery.json` e na matriz `final-matrix.json` após a execução.
Qualquer falha de build/pacote bloqueia a entrega, sem testes sobre artefatos velhos.

Não publicar v1.0.0 definitiva. A RC é candidata local verificável, com limites
e lacunas explícitos; não há publicação ou push autorizado nesta tarefa.
