# Preparação gráfica — prévia Windows

Primeiro incremento dos §§15–17 do checklist, em 2026-10-12. Executável:
`.local/build/win-amd64/src/desktop/Debug/x360-native.exe`. Abre sem terminal,
inclusive sem conteúdo. Ainda exige a árvore do projeto, LLVM e ferramentas
MSVC/CMake existentes; não é o pacote autônomo exigido pelo §18.

O catálogo vem de `titles/*/preparation.toml`, separado da interface Windows.
Os caminhos de execução permanecem nos manifestos existentes. A 17559 usa
`x360n session`; não há botão para iniciar um EXE fixo de jogo. A 6717 aparece
com seu bloqueio de fonte e sem sessão guiada habilitada. Castle Crashers aparece
somente para preparar/verificar; seu lançamento continua na My Games original.

A importação aceita pastas extraídas do conjunto 17559. Verifica todos os
componentes exigidos por hash antes de escrever; copia só ausentes, publica bytes
completos sem sobrescrever destino concorrente e mantém originais. A associação
`xam.xex` → `system/xam_ui.xex` só ocorre para o hash declarado pelo alvo.
Procedência fica em `.local/logs/imports/`. Um diretório temporário deixado por
queda abrupta é informado como bloqueio, sem apagar arquivos silenciosamente.

Preparar aciona `build-title.ps1` em processo sem console, com captura de logs e
cancelamento pelo Job Object já existente. Os títulos são preparados em sequência.
Um marcador em `.local/preparation/<alvo>.incomplete` impede usar um executável
antigo como prova de uma tentativa interrompida; o próximo build bem-sucedido o
remove. Abrir uma sessão pronta não recompila. A interface não baixa ferramentas,
instala dependências, altera saves nem envia diagnóstico automaticamente.

Validação deste incremento:

- Modelo: 9 testes / 32 asserções passaram, com entradas sintéticas para ambiente vazio, importação com
  alias, versão errada, destino diferente, isolamento de saves, cancelamento,
  alteração posterior, caminhos com acentos, DLL ausente e preparação interrompida.
- Processo/sessão: 11 testes / 62 asserções passaram, incluindo o novo modo sem
  console e os contratos existentes de pedido original e retorno.
- Interface executada em ambiente vazio e no projeto real; capturas locais
  `desktop-empty.png` e `desktop-ready.png` em `.local/logs/`. A instância para uso
  manual foi reaberta na sessão normal, pois a execução isolada não ficava visível
  ao usuário.
- Preparação real chegou ao configure de Castle Crashers; o build completo pela
  interface não foi aprovado. Sessão `20261012T144205Z-session-0-dash_17559` abriu
  a intro, retornou pelo export original e entrou na dashboard; terminou pelo
  fechamento da janela. Não houve lançamento do jogo nessa execução.
- Iruka: `20261012T143832Z-regress-iruka`, L1–L5 passaram, 325 cores, diferença
  entre quadros 5,186, zero erros/críticas/stubs; término por timeout diagnóstico.

O sandbox negou a publicação por renomeação de arquivos sintéticos com erro
Windows 5. A suíte de importação foi repetida fora dele, como usuário normal;
isso não exigiu execução como administrador. Não confundir essa restrição de teste
com uma correção de compatibilidade do sistema de arquivos.

Pendências: importação guiada STFS e validação completa de assets dos jogos,
instalação automatizada da toolchain, estimativa de downloads/espaço, escolha
independente da pasta de dados, inglês, controle na interface de preparação,
atualização/rollback e aceite em máquina limpa. Perfis e saves também continuam
bloqueando o fluxo completo de release. Nenhuma release foi criada.
