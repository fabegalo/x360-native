# Interface desktop (x360-native.exe)

Gerenciador da plataforma: extrair → abrir → importar → preparar → iniciar o
Xbox 360. A dashboard original continua sendo a experiência de console e o
fluxo de execução (`x360n session`) não mudou. Esta página descreve a camada
visual redesenhada em 2026-10-15; fatos medidos e limites ficam no fim.

## Identidade visual

Win32/GDI com GDI+ somente para formas suavizadas; sem Electron, WebView ou
dependência externa (`gdiplus` e `uxtheme` são do Windows). Código em
`src/desktop/win32/ui_theme.h` (paleta, métricas, primitivas) e `src/desktop/win32/main_win32.cpp`.

- Tema escuro único: grafite (`#0D0F12` página, `#0A0C0E` barra lateral,
  `#161A1F` cards, `#1F242B` controles) e verde Xbox (`#107C10` ação, `#60D660`
  destaque). Estados: verde = pronto, âmbar = atenção/não suportado, vermelho =
  erro, azul = preparando/informação, cinza = importado.
- Tipografia Segoe UI (14 px corpo, 12 px legenda, 22/30 px títulos) e ícones
  Segoe MDL2 Assets. Tudo escala por DPI (`ui::S`), com `PER_MONITOR_AWARE_V2`.
- Componentes: botão primário/secundário/fantasma, item de navegação, chip de
  filtro, cartão de opção (renderer), interruptor, selo de estado, card, banner
  de atividade/resultado, barra de progresso por etapas, lista de cartões.
- Hover, selecionado, desabilitado, carregando e foco visível (anel verde de
  2 px, só no uso do teclado) em todos os controles.

## Telas

| Tela | Conteúdo |
|---|---|
| Início | Estado da dashboard configurada, ação principal (Iniciar/Configurar Xbox 360), resumo (dashboards preparadas, jogos importados, jogos preparados, renderer global) e atalhos |
| Biblioteca | Lista de cartões (nome, Title ID, versão, selo Pronto/Preparando/Importado/Não suportado/Erro), busca (Ctrl+F), filtros Todos/Prontos/Preparar/Atenção, painel da seleção com a ação disponível |
| Importar | Área de soltar arquivo/pasta, ISO/GDFX, XBLA/STFS e pasta extraída; progresso por etapas (Extração → Verificação → Importação) |
| Dashboard | Versões da dashboard original, estado de cada preparação e requisitos (arquivos originais, preparação AOT, ambiente do PC) |
| Configurações | Renderer global (Xenos padrão; Native experimental somente com o acesso de desenvolvimento existente), armazenamento e ambiente, idioma PT-BR/EN do desktop, dados do PC e idioma/localidade independentes do console ([contratos e validação](console-language-locale.md)) |
| Diagnóstico | Detalhes, requisitos e logs da operação |

O banner no topo mostra a operação em andamento (título, mensagem, tempo,
etapas, Cancelar) ou o resultado (sucesso/aviso/erro) com **Tentar novamente**,
**Diagnóstico** e **Fechar**. A janela é redimensionável (mínimo 1000×700 px a
96 DPI) e a navegação por teclado segue a ordem Tab; Esc volta de Configurações/Diagnóstico.

## O que o progresso mostra de verdade

Não há percentual de ISO/STFS disponível no extrator, então a barra **não
inventa porcentagem**: etapas concluídas ficam sólidas e a etapa atual anima.
Mostra-se tempo decorrido e, na extração, os bytes já gravados na pasta de
extração (medidos a cada segundo). As etapas de preparação vêm dos marcadores
`X360N_STAGE` dos logs (ferramentas, configuração, tradução, compilação) e do
registro final. Importação continua cancelável; o original nunca é alterado.

## Estado e serviços

Nenhum serviço mudou: `catalog`, `import_source`, `imported_games`, `library`,
`generic_aot`, `workspace`, `renderer_settings`, o supervisor e os scripts de
preparação são os mesmos. A UI só adiciona estado de apresentação (`Geo`, filtro,
busca, banner, etapas). Todos os trabalhos pesados continuam na thread de
trabalho; a thread de UI só pinta e recebe mensagens.

## Revisão visual e testes locais (sem Computer Use)

Opções somente de builds fora da distribuição: `--lang pt|en`, `--dpi N`
(substitui o DPI para testar escala), `--import <caminho>` (inicia a importação
ao fim da primeira verificação) e a mensagem de pré-visualização `WM_APP+20`
(estados sintéticos em memória: importando, preparando, erro, sucesso, 9 jogos,
biblioteca vazia). Nada disso grava dados do usuário.

```powershell
# Capturas PNG de todas as telas (PrintWindow, sem injetar entrada)
powershell -ExecutionPolicy Bypass -File scripts/diagnostics/capture-desktop-ui.ps1 -Exe <x360-native.exe> -OutDir .local/evidence/ui/after [-Dpi 144 -Width 1500 -Height 960] [-Lang en] [-Tag min]
# Verificações funcionais num ambiente descartável (copia só titles/ e deps.lock.json)
powershell -ExecutionPolicy Bypass -File scripts/validation/test-desktop-ui.ps1 -Exe <x360-native.exe>
# Biblioteca persistente entre dois processos (existente; adaptado ao controle de lista)
powershell -ExecutionPolicy Bypass -File scripts/validation/test-library-desktop.ps1 -Executable <x360-native.exe> -Root . -OutputDirectory .local/evidence/ui/library-test
```

As capturas ficam em `.local/evidence/ui/{before,after}` (ignorado pelo Git): mostram
caminhos e jogos locais do usuário, por isso não são versionadas.

## Medições (2026-10-15, Release, mesmo ambiente descartável, 10 s)

| | Antes | Depois |
|---|---|---|
| CPU ociosa | 0,00 % | 0,00 % |
| Working set | 28,4 MB | 30,0 MB |
| Handles | 298 | 320 |
| Animação durante operação | — (barra marquee do sistema) | 2,2 % de um núcleo (6,6 % antes de limitar a invalidação à barra) |

A interface só repinta quando há mudança; a animação existe somente durante
uma operação e invalida apenas a barra e o relógio.

## Limites conhecidos

- A página Configurações não rola: abaixo de ~700 px de altura (96 DPI) o conteúdo
  é recortado; o mínimo da janela impede isso, mas telas menores que 700 px não foram avaliadas.
- DPI foi exercitado pelo override (`--dpi 144`), não por troca real entre monitores.
- Sem modo de alto contraste, sem tema claro; PT-BR e inglês somente.
- Controles desenhados pelo aplicativo expõem texto por `LBS_HASSTRINGS`/títulos
  de janela, mas não houve validação com leitor de tela.
- O drop de arquivos foi preservado (`WM_DROPFILES`), mas o teste automatizado usa
  `--import` porque `HDROP` não atravessa processos por `SendMessage`.
- Pacote de distribuição (`X360N_DISTRIBUTION`) só teve verificação sintática do
  `main_win32.cpp`; `package-desktop.ps1` não foi executado.
