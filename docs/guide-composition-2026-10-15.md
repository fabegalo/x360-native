# Guide original 17559 — composição ainda incompleta

Investigação e execuções locais de 15–16/10/2026, Windows x64/Xenos.

> Atualização de 16/10: a composição universal foi implementada pelo buffer de
> sistema original; ver [relatório de 16/10](guide-composition-2026-10-16.md).
> As constatações abaixo descrevem o estado anterior.


O objetivo de universalizar o Guide **não foi concluído**. O host continua
dependente do ponto de desenho da dashboard e dos hooks de resolve do Forza.
Não foi implementado um compositor genérico no runtime/Xenos. Não há aceite
visual completo nem garantia de retorno ao jogo em todos os títulos.

Este trabalho usa os módulos, fontes, skins e cenas originais já identificados
no perfil 17559. Não acrescenta desenho de menu, rodapé ou escurecimento emulado.
Native continua suspenso. Não houve Computer Use, push ou Release.

## Correções realizadas

- O host passa a usar o contexto da thread que efetivamente o chama, inclusive
  nos callbacks de desenho e destruição. Não retém a thread que abriu o Guide.
  `SystemUiContext` restaura os registradores PPC, FPSCR e identidade do processo
  também durante desenrolamento de exceções; mantém a identidade de sistema
  apenas durante a chamada ao original.
- Uma nova borda do botão Guide solicita o fechamento do Guide ativo pelo
  contrato XAM existente. Não fecha outro aplicativo de sistema por esse atalho.
- No retorno Xbox Home, o host aguarda a saída nativa da thread que executou
  o Guide antes de destruir callbacks e publicar o handoff. A saída da thread
  principal sozinha não significa que `TerminateTitle` terminou. Chamadas de
  quadro sobrepostas são recusadas enquanto essa instância está em execução.
- A inicialização de idioma, strings e fontes executa a rotina original
  `816C8450`, como o host original `816CE8B0`. Remove a montagem parcial do
  descritor de fonte, que omitira parte da seleção feita pelo original.
- A ponte interna de alocação `816E67E8` preserva o alocador físico original
  quando solicitado por `0x40000000`; o adaptador do heap virtual rejeitava essa
  classe. Isso não inicializa sozinho o compositor.
- O runner diagnóstico guarda argumentos, hashes de binários, eventos com
  horários reais, screenshots e resultado. Saída não zero, timeout de captura
  e encerramento forçado são falhas. Fechar a janela ao final não conta como
  Xbox Home nem como retorno pelo supervisor.

## O que falta no caminho original

A investigação do AOT local 17559 identificou partes distintas que não podem
ser reduzidas à chamada de desenho do HUD:

| Parte original | Evidência local |
| --- | --- |
| Recursos do compositor, shaders e alvo próprio | `816CA748` |
| Passo de fundo e transformação do vídeo | `816C9FF0` |
| Evolução do fade | `816C7F20` e solicitação `816C9CE0` |
| Gravação, desenho, resolve e publicação do quadro de sistema | `816CCF00` |
| Publicação do descritor de comandos para o kernel | `81842B08` → `VdSetSystemCommandBuffer` |

Na revisão fixada, `VdSetSystemCommandBuffer`, `VdQuerySystemCommandBuffer` e
`VdRegisterXamGraphicsNotification` são stubs. `VdGetSystemCommandBuffer` devolve
um bloco zerado com marcadores. O host atual empresta o dispositivo do título;
não inicializa o contexto gráfico global de sistema necessário à gravação
independente dos comandos originais.

Mover simplesmente `DrawSystemUiFrame` para `VdSwap` não resolve essa lacuna:
o front buffer fornecido pelo título já foi resolvido. Tampouco há isolamento
automático dos registradores, recursos e EDRAM do jogo ao executar outro fluxo
de comandos na mesma GPU. A implementação desses contratos e da composição
continua pendente; os hooks existentes foram preservados.

Uma referência visual consultada foi a captura do Guide Metro no
[artigo de arquitetura do Xbox 360](https://www.copetti.org/writings/consoles/xbox-360/),
associada pelo autor ao [tour de 2012 de Jeremy Hill](https://www.youtube.com/watch?v=olTeUQbIjoU).
A captura mostra a separação visual do Guide e o fundo escurecido. Não é uma
captura identificada como 17559 e não certifica equivalência pixel a pixel.
As decisões técnicas acima vêm dos originais locais, não dessa referência.

## Evidências e limites

Diretório local: `.local/validation/universal-guide/`. Conteúdo proprietário,
screenshots, binários e código AOT derivado permanecem fora do Git. Os diretórios
de execução contêm `environment.json`, `scenario.json`, `events.jsonl`,
`result.json` e logs; os hashes identificam os binários efetivamente executados.
Os testes usaram cópias dos perfis locais, não o diretório de perfis em uso.

Os cenários explícitos estão em `tests/local/guide-dashboard.json`,
`guide-castle.json` e `guide-forza.json`. Exemplo de reprodução diagnóstica:

```powershell
pwsh -NoProfile -File scripts/validate-original-guide.ps1 `
  -Executable .local/build/titles/dash_17559/dash_17559.exe `
  -GameRoot .local/inputs/dashboard/17559 `
  -SeedUserData .local/validation/universal-guide/baseline-user `
  -ScenarioFile tests/local/guide-dashboard.json `
  -OutputDirectory .local/validation/universal-guide/nova-execucao
```

Para jogos, selecionar o executável/pasta/cenário correspondentes e acrescentar
`-SystemUiHost .local/build/titles/dash_17559/dash_17559_session_ui.dll`
e `-SystemUiRoot .local/inputs/dashboard/17559`. Usar um diretório de saída novo
e builds válidos; esses comandos não substituem os recibos de preparação nem
o teste de sessão completa pelo supervisor.

| Execução | Resultado observado |
| --- | --- |
| `baseline-dash` | Guide original com texto da dashboard visível atrás dos comandos; evidência anterior às correções |
| `dashboard-context` | Mudar a ordem da chamada XUI não eliminou a sobreposição; alteração de ordem retirada |
| `dashboard-final` | 52,45 s, 20 eventos, saída 0, sem encerramento forçado; navegação entre abas, B, reabertura e fechamento pelo Guide |
| `dashboard-original-font` | 28,41 s, 6 eventos, saída 0; rotina original de localização retornou sucesso; abertura, Games & Apps e fechamento |
| `dashboard-profile` | 40,41 s, 10 eventos, saída 0; Settings → Profile abriu a cena original com o perfil Forza; B voltou à dashboard e G reabriu o Guide |
| `castle-current` | Build local anterior do jogo com host 17559 novo; 30,47 s; Guide não apareceu após G; saída 0 após fechamento da janela |
| `forza-current` | Host novo sobre o build local anterior do jogo; Guide abriu, navegou e fechou na tela inicial; textos incompletos e encerramento forçado após 5 s de espera; **falha**, não aceite |
| `forza-rebuilt` | SDK atual; abertura, navegação, fechamento e confirmação Xbox Home; handoff Dashboard, mas crash `-1073741819` durante desmontagem; **falha** |
| `forza-return-attached` | Reproduziu o crash com debugger: `KernelState::TerminateTitle`, `kernel_state.cpp:1065`, concorrendo com desmontagem após a saída da thread principal |
| `forza-exit-fixed` | Após aguardar a thread do Guide: 76,88 s, 14 eventos, saída 0, sem cleanup; debugger `exit=0 captured=0`; handoff Dashboard, flags 0, 1020 bytes |

As capturas da dashboard mostram texto legível nas abas observadas, ícones de
botões, foco verde e perfil local `Forza`. Mostram também defeitos: o texto da
dashboard atravessa visualmente os comandos do Guide e há um retângulo no lugar
do símbolo ao lado do gamerscore. Um retângulo preto no canto inferior direito
já aparece antes de abrir o Guide; sua causa não foi estabelecida. A captura de
janela também pode incluir a notificação do driver NVIDIA.

A rotina original de fontes não eliminou os defeitos visuais nessas capturas.
Não há aceite de todos os glifos, cores ou layout, de animações completas, de
idiomas adicionais ou de troca de perfil nessa revisão. Xbox Home foi observado
no Forza até a saída limpa e entrega do handoff, sem relançar a dashboard pelo
supervisor nesse teste. As capturas são pontuais, com o atraso real do helper registrado; não
constituem medição de duração de animação. Castle foi observado na introdução,
Forza na tela inicial. Não são testes de condução/campanha ou de saves.
Ao sair de Profile, o host não restaurou a aba anterior do Guide: essa navegação
permanece incompleta. O nome `closed.png` no cenário de perfil não é um veredito:
nessa captura o último G reabriu o Guide depois do retorno à dashboard.
No Forza recompilado, textos de itens e letras dos botões continuam incompletos;
o diálogo Xbox Home apresenta o mesmo defeito nas opções. A correção da corrida
de saída não corrige essa renderização. Avisos de liberação do heap/tabela de
funções continuam no encerramento, mesmo na execução sem exceções.

O diagnóstico `forza-return-debug` foi interrompido porque a consulta CIM não
estava disponível no sandbox. `forza-return-debug-2` não reproduziu a sequência
de retorno e foi encerrado em 110 s; não é sucesso nem reprodução do crash.
O debugger de `forza-return-attached` foi anexado ao PID registrado pelo runner,
com acesso entre processos autorizado, e capturou a pilha efetiva da exceção.

### Experimentos retirados

`device-probe`, `recording-probe` e `physical-probe` são diagnósticos temporários,
não funcionalidades habilitadas no código final. O primeiro criou o dispositivo
original de gravação tipo 2 e um command buffer. O segundo mostrou a rejeição da
alocação física e terminou com acesso inválido a `0x00003C28` durante a tentativa
de gravação; saída `-1073741819`. O runner ainda não convertia saída não zero
após o último evento em falha: o campo `failure:null` dessa execução é incorreto.
O crash permanece registrado e não foi reclassificado como sucesso.

Depois de preservar o alocador original, `physical-probe` alocou memória física
em `FE663000`, mas constatou `VdGlobalXamDevice == 0`. O experimento recusou
prosseguir, antes da gravação. Inicializar apenas o dispositivo tipo 2 não
estabelece o contexto global de alocação do sistema. O experimento foi retirado
e a dashboard recompilada antes das validações seguintes.

### Build, contratos e preservação

Builds via `scripts/devshell.cmd`; logs `dash-final-build.log` e
`dash-original-font-build.log`. Os testes de contexto foram compilados no alvo
`x360n_sdk_runtime_checks` Release (`context-build-fixed.log`).
O Forza foi recompilado com o SDK atual antes da repetição (`forza-build.log`,
saída 0), incluindo a atualização das DLLs pelo staging do projeto.
O host com a correção de saída foi recompilado em `dash-exit-build.log`;
`exit-test-build.log` e `exit-overlap-test-build.log` registram os testes novos.

- `[guide-context]`: 1 caso, 7 assertions, incluindo duas threads distintas,
  escopos aninhados e exceção sintética.
- `[profile-ui],[signin],[guide-contract],[title-return],[guide-context]`:
  21 casos, 461 assertions aprovadas em `contracts-workspace-temp.log`.
- Após a correção de saída: 22 casos, 470 assertions aprovadas em
  `contracts-final.log`. O teste novo mantém o chamador de Xbox Home ativo,
  verifica a recusa de outro quadro simultâneo e comprova que a liberação
  aguarda a saída nativa do chamador, mesmo sem retorno pelo escopo C++.
- A tentativa anterior com TEMP padrão teve 2 casos/4 assertions em falha
  (`contracts-test.log`), em criação de perfil e leitura de launch request.
  A repetição usou TEMP/TMP dentro do workspace, sem alteração de código entre
  as duas execuções. O resultado anterior não foi apagado.
- `runner-exit-check` verifica que uma saída não zero sem eventos pendentes
  agora gera falha e erro do script. Usa a rejeição de argumentos pelo executável
  de testes, sem executar jogo.
- `apply-sdk-patches.ps1 -Check`: aprovado, `sdk-preservation.log`. SDK fixado e
  76 patches preservados, sem novo patch ou alteração manual no submódulo.

Não houve suíte completa nem medição comparativa de performance. Não se declara
ganho de FPS, ausência de regressões em todos os jogos ou conclusão do Guide
universal a partir desses resultados.
