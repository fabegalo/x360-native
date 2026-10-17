# Xenos X2 — fetch predicado e integração

Validação de 2026-10-15 na branch `codex/xenos-forza-correctness`, baseada em
`ee7e8af`. Native permanece suspenso. O SDK continua em `f5337cdc`; nenhuma
mudança no renderer Native, Vulkan, desktop ou originais de outras versões.

## Diagnóstico reproduzido antes da correção

A DLL de diagnóstico preservou o patch 0076 e acrescentou somente um log local
limitado às primeiras 256 rejeições de descritor. Nenhum bypass foi habilitado.
Reprodução: `.local/evidence/benchmarks/xenos-x2-diagnostic/forza_horizon/`.
O log foi preservado antes da rotação em
`.local/evidence/validation/xenos-x2/first-fetches.log`.

| VS (hash do microcode) | Amostras | c127.x |
|---|---:|---|
| 4E1DA281CC3D7EDB | 72 | 00000000 |
| C34795A841E7DEFF | 61 | 00000000 |
| D34A83D9E6B3A399 | 62 | 00000000 |
| F9E04B937F210086 | 61 | 00000000 |

No primeiro shader, ALU 10 executa `setp_eq` de `c127.x` contra zero.
O EXEC seguinte e o `vfetch_full` 12 usam `!p0`; formato 10_11_11, stride 1,
constante física 90 (o disassembler apresenta `vf5`, pela numeração 95−índice).
O descritor é `00000001 00000000`, tipo invalid vertex, endereço/tamanho zero.
As três outras variantes usam a mesma condição de constante; os dumps ficam
locais em `.local/evidence/validation/xenos-forza/shaders/`.

**Erro real de submissão, não leitura inválida demonstrada:** com c127.x zero,
p0 é verdadeiro para todas as invocações e o fetch não executa. Entretanto,
`D3D12CommandProcessor::IssueDraw` percorre o bitmap estático de fetches do
shader, valida o descritor sem considerar predicação e retorna false. O comando
`PM4_DRAW_INDX` registra falha de backend e o draw inteiro desaparece. Essa
rejeição acontece antes de `RequestRange` e do envio do draw: não é evidência
de uma leitura CPU/GPU no endereço zero. Os shaders incluem passes sem PS e
passes com PS; não é correto classificar todos como draws sem efeito.

O contrato de `setp_eq` está em `format/ucode.h`; o tradutor DXBC emite a
comparação com zero e mantém a predicação antes de calcular/ler o fetch.
O upstream também documenta explicitamente a limitação da validação antecipada:
[fetch DXBC do Xenia](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/dxbc_shader_translator_fetch.cc),
[checagem D3D12](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/d3d12/d3d12_command_processor.cc) e
[semântica do microcode](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/ucode.h).
Referências consultadas em 2026-10-15; implementação e testes usam a revisão
local pinada, sem executar Xenia nem substituir o executor AOT.

## Correção genérica

Patch reproduzível `0077-inactive-vertex-fetch.patch`: somente no caminho de
descritor inválido do Xenos D3D12, provar que todas as referências ao fetch
ficam inativas antes de dispensar a exigência de residência. O shader GPU,
o descritor e a memória permanecem originais. Nenhum índice, título, hash de
shader ou constante específica é codificado na implementação.

A análise mantém um conjunto conservador de valores possíveis de p0. Entende
EXEC linear, condições booleanas e `setp_eq/ne` de constante absoluta, incluindo
swizzle, zero com sinal, denormais conforme comparação DXBC, NaN e infinito.
Predicados por vértice, alterações desconhecidas de p0, jumps, calls, loops,
terminação condicional e endereçamento relativo não recebem prova especulativa.
Referências repetidas e mini-fetches também são verificadas. Falta de prova
mantém o diagnóstico/rejeição anterior; descritores ativos nunca recebem
valores inventados nem residência fictícia.

O resultado não é armazenado no bitmap de residência: uma atualização de
constante pode ativar o fetch no draw seguinte sem alterar seu descritor.
Descritores válidos não percorrem o shader; a análise não aloca heap nem faz
logging. Não se adicionou instrumentação permanente. O patch 0076 e os waits,
barreiras, shaders traduzidos e caches existentes foram preservados.

## Testes

- `inactive_vertex_fetch_probe.cpp`: 2.328 verificações, zero falhas. Abrange
  todos os 96 slots e quatro índices de constantes, transições ativo/inativo,
  NaN/inf/denormais, predicação de instrução e EXEC, origem desconhecida,
  constantes relativas, bool constants, segundo uso ativo, mini-fetch e
  microcode truncado/fora do limite. Target `sdkcheck.inactive-vertex-fetch`.
- Resolve vazio: 204 verificações, zero falhas, mesmo helper/objeto do 0076.
- Os quatro shaders diagnosticados passam a prova com o valor observado.
  Uma varredura exploratória dos dumps com registradores zerados encontrou
  87 candidatos em 267 VS; **isso não certifica draws reais dos outros shaders**.
- Compilação pelo `devshell.cmd`, reutilizando objetos inalterados do AOT/SDK.
  Somente o command processor Xenos foi recompilado para a DLL final. Não houve
  extração, codegen, rebuild dos títulos, troca de artefatos de produção ou
  suíte completa. Comandos/logs locais: `validation/xenos-x2/build/`.

Forza com DLL final: `.local/evidence/benchmarks/xenos-x2-fixed/forza_horizon/`.
Chegou à condução com carro, estrada, vegetação, linha de corrida e HUD visíveis.
Zero avisos de vertex fetch 90, zero falhas de backend e zero resolve vazio.
Os nove erros restantes são de VFS/XAM/liberação de tabela já fora deste fix;
avisos de texture fetch inválido permanecem, sem supressão ou alegação de
inocuidade. Encerramento limitado do runner não conta como retorno à dashboard.

Castle: a primeira coleta retornou originalmente à dashboard (flags 0x100,
exit 0) aos ~45 s, antes da janela 75–105 s. Foi registrada como janela inválida,
sem inventar métricas. A coleta focada `xenos-x2-castle-focused` aceitou Start,
mostrou o menu original e registrou zero errors/criticals, três warnings.
Janela 25–35 s: 58,804 FPS de swaps, P95 18,499 ms e P99 34,206 ms.
É regressão curta de menu, não certificação de campanha ou save/load.

Nenhum ganho de FPS é alegado. A captura corrigida de Forza está na condução
do Viper, enquanto a comparação histórica 0076 chegava a outra cena/câmera.
As métricas ficam retidas, mas não são A/B comparável. A coleta diagnóstica
teve dumping/log adicional e trabalho de compilação concorrente, e não serve
como baseline de desempenho.

## Ciclo real em isolamento

O ciclo completo em `.local/evidence/validation/xenos-x2/cycle/` usou o AOT existente,
DLL final nos dois títulos, cópias da Biblioteca/perfis/saves e somente os
14 recursos originais verificados da Retail 17559. `xenos_cycle_probe.cpp`
reutiliza a classe `SessionWindow` e o protocolo de launch request do supervisor.
Somente lança Forza quando o pedido original aponta para a instalação esperada
e seu SHA-256 corresponde; só retorna à dashboard diante de um pedido original
`Dashboard`. Não escreve handoffs nem fabrica identidade/perfil.

| Fase | PID | HWND pai | Resultado |
|---|---:|---:|---|
| Dashboard 17559 / My Games | 28920 | 3475964 | exit 0; Image, flags 0, 1020 bytes |
| Forza / Guide / Xbox Home | 33896 | 3475964 | exit 0; Dashboard, flags 0, 1020 bytes |
| Dashboard retornada | 13396 | 3475964 | Home renderizada; três anexos na sessão |

O Guide original mostrou o perfil **Forza**. Xbox Home abriu a confirmação
original; Up/Yes e A produziram o pedido de retorno. XUID
`E00AD9FD043F47E5`, state=1 e o mesmo profile root aparecem nos três processos.
`local-v1.dat` permaneceu com SHA-256
`81706351ae0f3eb4db42bd52050f86cbf3303bb9463680be67ab46221fb24705`.
Zero timeout/crash nas transições, zero erros de resolve vazio/fetch 90/draw.
A dashboard retornada permaneceu viva por ~41 s; WM_CLOSE ao final foi **limpeza**,
com exit 0 e handoff WindowClose, não foi usado para simular Xbox Home.

Entrada foi demonstrada em My Games, Start do Forza e Guide/diálogo de retorno.
**A navegação por controle após o retorno não ficou certificada:** a captura
após RB não mostra mudança de seleção. A passagem adicional `cycle-final`
foi limitada a 240 s e terminou ainda na dashboard (harness result 9 após
limpeza WindowClose); não é um segundo ciclo aprovado. Os primeiros comandos
no sandbox também não comprovaram entrega. Os passos aprovados usaram scripts
locais no host, com PostMessage verificado e restrito ao PID da sessão; nenhuma
ferramenta Computer Use foi usada. A evidência inconclusiva foi preservada.

Capturas principais: `forza-selected.png`, `forza-running.png`, `guide-open.png`,
`xbox-home-shortcut.png`, `xbox-home-yes.png`, `dashboard-return.png`.
O jogo chegou ao menu Single Player neste ciclo; a condução foi verificada na
regressão separada. Há baixo contraste/contorno em textos do Guide nas capturas;
nenhum teste pixel a pixel contra console ou baseline certifica esse aspecto.
Não se declara fidelidade gráfica integral a partir da ausência de erros no log.

**Limite de integração de produção:** o Forza preparado no checkout principal
já tinha `.incomplete`, e a nova DLL invalida o recibo anterior. Este harness é
uma validação diagnóstica dos originais/AOT e de seu handoff, não uma execução
recertificada de `x360n session`. O supervisor e seu gate não foram modificados,
e nenhum recibo artificial foi publicado. Distribuição/preparação normal da
DLL continua necessária antes de usá-la pelo launcher de produção. Evitou-se
rebuild completo e interferência com o trabalho desktop paralelo.

## Artefatos e preservação

Todos os caminhos `.local` citados nesta nota estão sob
`C:/Users/fabeg/Desktop/x360`, não sob o worktree de código. Scripts, comandos,
binários e evidências ficam em `validation/xenos-x2/`; shaders e conteúdo
proprietário não são incluídos no commit. `evidence.json` registra hashes.
Os dois arquivos do patch foram reaplicados numa árvore contendo o estado
anterior e o `.gitattributes` do SDK, reproduzindo exatamente seus SHA-256
(`reapply.json`). A checagem normal dos 76 patches ativos passou (0073 retirado).
O hash e o conteúdo do patch 0076 continuam iguais aos de `ee7e8af`.

O target manual `x360n_xenos_cycle_probe` é `EXCLUDE_FROM_ALL`, sem registro
CTest: não autoriza nova sessão gráfica automática. A execução usada teve
limite de 600 s no Job Object/runner, com encerramento de todos os processos.
O fixture recebe a raiz original e a pasta de diagnóstico com `dash-bin`,
`game-bin`, `dash-user`, `game-user` e `expected-library-path.txt` verificados
previamente; nunca usar raízes de usuário de produção nessa pasta.

Sem push/Release, alterações de SDK pinado, módulos de outra dashboard,
renderer Native/Vulkan, desktop, Biblioteca ou saves reais.
