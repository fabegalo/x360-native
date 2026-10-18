# Charlie Murder — registro My Games e gates AOT (18/10/2026)

## Escopo e identidade

Conclusão da integração local usando o projeto AOT existente do Charlie Murder,
Title ID `58410B83`, versão `0.0.0.2`. Nenhum gameplay, Computer Use, teste visual,
push ou Release foi autorizado/executado nesta etapa. Após o diagnóstico, o
mantenedor autorizou expressamente recompilar apenas o Charlie para renovar seu
recibo diante das mudanças pré-existentes do SDK.

- Projeto genérico:
  `g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609a48d5f5b47`.
- Build compacto: `.local/build/aot/g0a4c6253b34297dc/`.
- XEX de entrada:
  `0e9d4f0967f8dd121c0c93694b601d30cad66d7a87fb4cbb0b9e3325c3d7b4d1`.
- Inventário importado: 983 arquivos; 32 módulos dependentes declarados no
  projeto, com identidade própria. Recursos originais da dashboard permanecem
  exclusivamente do perfil Retail 17559.

## Reprodução da indisponibilidade

A tentativa real de abrir `.local/environment.lock` com compartilhamento
exclusivo falhou por outro processo já possuir o arquivo. A existência do arquivo
não é uma trava residual: o contrato depende do handle aberto.

O Windows Restart Manager, usado somente para consulta
(`RmStartSession`, `RmRegisterResources`, `RmGetList`, `RmEndSession`), identificou
o PID **65016**, `powershell.exe -NoProfile -ExecutionPolicy Bypass -File
scripts/build-title.ps1 -Title dash_17559`. O log correspondente progredia na
compilação nativa da dashboard. A consulta inicial dentro do sandbox não
enxergava os processos externos; uma consulta somente leitura fora dele
identificou o build e seus filhos. Nenhum processo/handle do usuário foi fechado,
nenhum lock ou marcador foi removido manualmente.

O gate local de registro também recusou a dashboard em preparação:
`[.local-prepared]`, uma falha em três asserções, exit 42, antes de qualquer
registro. Essa falha foi preservada, não classificada como regressão do launcher.

## Recibo anterior e preparação autorizada

O recibo anterior do Charlie tinha 3.463 inputs e 35 artefatos. Todos os
artefatos e globs conferiam, mas duas entradas haviam mudado:
`deps.lock.json` e `external/rexglue-sdk/src/codegen/function_scanner.cpp`, em
decorrência do patch 0092 pré-existente. SHA-256 do recibo anterior:
`db86d0a1f27fadaca564e6207c360039b26fefe889653ffdb34372b1e5b1a524`.
Não bastava reencontrar o EXE: registrar esse build como preparado no ambiente
atual violaria a proveniência certificada. O recibo foi copiado para evidência;
nenhum hash foi reescrito para aprovar produtos antigos.

A preparação autorizada usa `scripts/build-title.ps1`, `devshell.cmd`, LLVM
22.1.8 e SDK pinado, dois jobs e watchdog de 1.800 segundos restrito à árvore
criada por esta execução. Builds independentes podem adquirir leases
compartilhadas do ambiente; a mesma árvore/título e sessões/registro continuam
mutuamente exclusivos. O build da 17559 já em curso não foi reiniciado.
Tempos desta preparação não constituem benchmark de performance.

## Validação sem executar títulos

Os testes opt-in de registro local foram ampliados para:

- Exibir os problemas da dashboard quando o gate de preparação falha.
- Restringir o registro ao jogo solicitado por `X360N_TEST_PREPARED_GAME`,
  preservando os headers de outros títulos.
- Conferir que os recibos do jogo/dashboard permanecem idênticos no registro.
- Revalidar os perfis pelo caminho de produção (`LoadSessionProfile`), descobrir
  o projeto genérico nos destinos da dashboard e resolver o caminho instalado
  com `MatchesLaunch` e SHA-256.
- Recusar raiz de outra dashboard, flags de lançamento não suportadas e
  fechamento de janela como pedido de imagem.
- Conferir o header de 328 bytes e todo o inventário instalado com
  `InspectLibrary`, sem conceder licença nem iniciar processos dos títulos.

`[.local-prepared]` continua exigindo os recibos válidos de ambos os títulos
antes de qualquer escrita. `[.local-game-registration]` exige o recibo válido
do jogo e os inputs originais completos da dashboard, validando apenas o
registro e os metadados de roteamento, como o fluxo de preparação de jogos do
desktop já faz. Este segundo teste não aprova a execução da dashboard: sua
leitura de catálogo usa `LoadSessionProfile(..., false)`, enquanto o perfil do
jogo é revalidado integralmente. Nenhum gate de produção foi alterado.

Essa validação verifica o contrato de roteamento/transição. Não comprova a
navegação visual na My Games, a execução de XNA, a inicialização funcional do
Charlie, seu retorno real, gameplay, saves comerciais ou performance.

Comando focado (somente após ambos os builds terminarem e os recibos conferirem):

```powershell
$env:X360N_TEST_PREPARED_ROOT = (Get-Location).Path
$env:X360N_TEST_PREPARED_GAME = 'g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609a48d5f5b47'
& .local/build/desktop-only/src/desktop/x360n_desktop_tests.exe '[.local-prepared]'
```

Para validar somente o registro, com o mesmo jogo explicitamente selecionado,
o filtro é `[.local-game-registration]`.

## Dependência 17559 e decisão do mantenedor

O build externo da dashboard chegou a `4-build: exit 0 in 816 s`, mas seu
watchdog global de 1.500 s expirou antes da publicação do recibo. O resultado
externo registra exit 124; o marcador `dash_17559.incomplete` e o recibo anterior
permaneceram. A compilação não foi promovida a preparação válida.

Informado desse bloqueio, o mantenedor escolheu **concluir Charlie e documentar
o bloqueio**, sem autorizar retomar a 17559. Portanto esta etapa não recompila
essa dashboard, não remove seu marcador e não certifica uma transição executada.
O registro do jogo pode ser conferido no armazenamento e catálogo original,
mas a sessão de produção continuará recusando a dashboard até uma preparação
posterior autorizada publicar seu próprio recibo válido.

## Evidências locais

Diretório ignorado `.local/evidence/charlie-library-2026-10-18/`:
consulta de donos da trava (`lock-owners.json`), auditoria inicial dos recibos
(`receipt-audit.json`), recibo anterior preservado, falha do preflight
(`registration-preflight.log`), build limitado e hashes dos headers anteriores.
Logs das etapas AOT ficam também no build compacto do Charlie. Os quatro headers
iniciais pertenciam a Castle, Motocross, Charlie e ContentCache; o do Charlie já
existia com SHA-256
`a290feb641914f6b171d6b54c7e45f3eecf2b90e9f7686eb2637fc3f85829f3d`.

## Harness e limites operacionais

O primeiro build do teste auxiliar atingiu o watchdog de 180 s depois de emitir
o link. A árvore remanescente não tinha mais Ninja/linker, mas retinha os wrappers
PowerShell/CMake. O cleanup por `taskkill` dentro do sandbox também falhou com
erro de credenciais. Foram consultados PID, pai e comando fora do sandbox;
somente a árvore auxiliar criada nesta tarefa foi encerrada. A repetição fora do
sandbox terminou com exit 0 em 6 s (`validation-helper-build-final`), sem trabalho
Ninja pendente. A primeira tentativa do Charlie foi igualmente interrompida
somente na árvore própria e reiniciada fora do isolamento pelo driver oficial,
sem reaproveitar manualmente etapas nem publicar recibo parcial. Não se atribui
esse comportamento do harness ao runtime do jogo.

A primeira interrupção deixou um guard descendente retendo o log de `2-tool`;
a tentativa seguinte falhou na abertura desse log (exit 1), antes de executar a
ferramenta. O Restart Manager identificou o PID 44964, guard do build compacto
do Charlie criado às 02:16:47 nesta tarefa. `Stop-Process` não o enumerava no
contexto local, embora a consulta WMI ainda o identificasse. A terminação WMI
desse único processo próprio, condicionada a PID, pai, comando e criação,
retornou 0. O log anterior foi preservado em `first-tool-interrupted.log`;
nenhum arquivo ou lock foi excluído para contornar o bloqueio. A nova tentativa
limitada está em `charlie-prepare-retry`.

O teste ampliado preservou uma segunda reprodução da recusa em
`registration-busy-diagnostic.log`: uma falha em três asserções, exit 42, com os
nomes dos produtos da 17559 ainda ausentes e seu marcador incompleto. O teste
termina antes de registrar qualquer conteúdo nessa situação.

## Resultado final: bloqueado, sem promoção de estado

Durante `charlie-prepare-retry`, outra atividade alterou
`scripts/build-title.ps1` e `scripts/build/preparation-receipt.ps1`. A checagem
de snapshots do driver recusa entradas alteradas durante a preparação; emitir
um recibo sobre essa mistura de revisões não seria válido. A tentativa foi
interrompida somente na árvore própria, identificada por PID, comando, criação
e descendência WMI, antes de nova compilação nativa do jogo. Foram preservados
o inventário da árvore e o resultado da interrupção. A etapa `2-tool` terminou
com exit 124 por essa interrupção; o wrapper registrou exit 1 após 513 s,
`timeout=false`. Isso não foi uma falha de compilação C++ nem o watchdog de
1.800 s. Nenhum recibo novo foi publicado.

O mantenedor escolheu então **documentar o bloqueio sem nova tentativa**. Não
houve retomada com os scripts atualizados nem incorporação de suas alterações
concorrentes aos commits desta tarefa.

| Verificação | Resultado |
|---|---|
| Build final do primeiro helper | Exit 0, 6 s; Ninja sem trabalho pendente |
| Build do helper com registro separado | Exit 0, 14 s |
| Gate final `[.local-game-registration]` | Exit 42; seis asserções, cinco aprovadas e uma falha em `inspection.prepared`, antes de `PrepareLibrary` |
| Recibo anterior do Charlie | SHA-256 inalterado; continua inválido para execução |
| Produtos do recibo anterior do Charlie | 32 artefatos ausentes na auditoria final, após a limpeza normal do driver autorizado; preparação incompleta |
| Headers existentes da My Games | Quatro hashes e tamanhos idênticos antes/depois |
| Registro novo e transição executada | Não validados; bloqueados pelos gates |

O teste final não passou: demonstrou a recusa correta do título incompleto.
Os ramos positivos novos compilaram, mas não foram exercitados no ambiente real
porque a preparação não terminou. Não foi executada suíte completa nem repetida
regressão visual/jogo de outro título.

O AOT anterior **não deve ser usado como preparado**: o driver iniciou a
recompilação autorizada e invalidou seus produtos regeneráveis antes da
interrupção. O recibo anterior é evidência, não um recibo novo de sucesso.
Inputs, conteúdo instalado, headers, perfis e saves não foram removidos por
esta tarefa; o marcador incompleto foi mantido.

Auditoria final em `receipt-audit-final.json`, gate em
`registration-gate-final.{log,result.json}`, comparação em
`headers-{before,after}.json` e encerramento em
`source-change-stop-tree.json`. Uma consulta final não encontrou processos
remanescendo na árvore própria interrompida. Ela observou outros builds externos
de 17559/Motocross segurando o ambiente; não foram interrompidos.

Para concluir futuramente, será necessário estabilizar os scripts, autorizar
nova preparação do Charlie e da 17559 e passar os gates de registro/execução.
Navegação visual e gameplay continuam dependendo de autorização própria. Não
houve push, Release ou alteração nos módulos originais de qualquer versão.
