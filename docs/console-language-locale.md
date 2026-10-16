# Idioma e localidade do console — 16/10/2026

Implementação Windows x64/Xenos, SDK fixado em `f5337cdc`, patch 0081.
O idioma do aplicativo desktop permanece independente do idioma do console.

## Preferências e sessão

Configurações → Console Xbox 360 oferece os 16 IDs de idioma da XAM e as 42
localidades nomeadas da tabela do runtime. Português (Brasil) é idioma **9**;
Brasil é país **13**, que a XAM converte para localidade **4**. País, localidade,
LCID do Windows e região de licença do XEX não são intercambiáveis.
Selecionar idioma não altera o país e vice-versa. Inglês/Brasil e
Português/Estados Unidos são combinações válidas.

O desktop salva `version`, `language` e `country` em `.local/console.toml`,
com substituição atômica. Arquivo ausente conserva o padrão anterior
inglês/Estados Unidos; arquivo inválido gera erro, sem sobrescrita silenciosa.
ID 10 de idioma e localidade 38 são reservados e não aparecem nas opções.
Os rótulos dos idiomas usam seus nomes nativos; não são traduções da dashboard.

O supervisor lê uma cópia das preferências no começo de `session`, remove
eventuais argumentos de idioma/país do manifesto e passa os valores globais
a cada processo, inclusive no retorno à dashboard. O host do Guide usa esse
mesmo runtime. Mudanças salvas durante uma sessão só entram na próxima.
Execução diagnóstica direta continua aceitando `--user_language` e
`--user_country`; ela não representa uma sessão preparada pelo desktop.

## Contratos XAM/XConfig

O patch 0081 corrige o `XGetLanguage` fixo em inglês, implementa
`XTLGetLanguageV2` e `XamGetCountry`, e mantém coerência de leitura com XConfig:

| Serviço | Resultado |
|---|---|
| `XGetLanguage` | Idiomas base 1–9; fallback da ABI para idiomas posteriores |
| `XTLGetLanguageV2` | Acrescenta polonês (11) e russo (12) |
| `XamGetLanguage` | Conjunto estendido até chinês simplificado (17), sem ID 10 |
| `ExGetXConfigSetting(3,9)` | Idioma em DWORD big-endian, tamanho 4 |
| `ExGetXConfigSetting(3,14)` / `XamGetCountry` | País, tamanho 1 no XConfig |
| `XamGetLocale` / `XamGetLocaleEx` | Conversão de país para localidade, preservando limites pedidos pelo chamador |

Valores inválidos não são truncados para virar outro país. Os getters de
idioma normalizam IDs inválidos para inglês. Para uma ABI antiga que não
suporta o idioma escolhido, o fallback é japonês no Japão, coreano na Coreia
e inglês nos demais países oferecidos. A configuração estendida permanece
disponível para consumidores que a suportam.

Referências de contrato: [Xenia Canary, xam_locale.cc](https://github.com/xenia-canary/xenia-canary/blob/master/src/xenia/kernel/xam/xam_locale.cc)
(`xeGetLanguage`, `XTLGetLanguageV2`) e
[enumeração XLanguage](https://github.com/xenia-canary/xenia-canary/blob/master/src/xenia/xbox.h).
As tabelas de países/localidades usadas pelo desktop foram conferidas contra
os exports do SDK fixado. Não foi alterada a região de licença do título,
a região AV ou o fuso horário.

As **escritas pela interface original** ainda não estão implementadas:
`XamSetCountry` retorna `ERROR_NOT_SUPPORTED` e `ExSetXConfigSetting` retorna
`STATUS_NOT_IMPLEMENTED`, em vez de deixar o registrador de entrada como
resultado. A seleção persistente desta entrega é feita pelo desktop.

## Originais e versões

Nenhuma string, XEX, XUI, fonte ou arquivo de conteúdo foi substituído.
A Retail 17559 continua carregando seu conjunto verificado de recursos.
Sua rotina original `816C8450` inicializa strings/fontes do Guide e consulta
o XConfig por `81699010`; não foi necessário sintetizar traduções nem
redirecionar essas leituras para dados de outra versão.

O original tenta caminhos regionais, por exemplo `media:\pt-BR\shrdres.xzp`;
esse arquivo separado não está no conjunto local. A dashboard resolve os
recursos existentes e mostra PT-BR nas capturas. Não se criou um arquivo
substituto para eliminar essa tentativa do log.

A lista de idiomas é a capacidade de configuração do console, **não uma
certificação de que cada mídia contém todos eles**. O título decide como
tratar traduções ausentes. Outras versões da dashboard mantêm seus próprios
recursos e limitações; não houve novo aceite visual de 6717/13604 ou de todos
os idiomas. Os testes de isolamento existentes permanecem intactos.

## Validação

Builds por `scripts/devshell.cmd`: desktop/supervisor e testes em Debug;
Retail 17559 e Forza Horizon em RelWithDebInfo. Logs em
`.local/logs/locale-build-*.log`. Nenhuma suíte completa foi executada.

- Preferências e processos: **3 casos, 800 asserções**. Inclui persistência,
  arquivo inválido, todas as combinações de opções, e dois processos do
  supervisor com dashboard → jogo → dashboard sintéticos. Uma alteração
  salva pelo primeiro filho só aparece na sessão seguinte.
- XAM/XConfig: **3 casos, 1.121 asserções**. Inclui ABIs, buffers, big-endian,
  PT-BR, todas as localidades do desktop contra a XAM real, país inválido,
  escritas não suportadas e recursos de fallback em dois runtimes.
- Patch novo aplicável sobre os quatro arquivos anteriores; hashes de
  entrada/saída registrados no lock, sem edição manual do SDK.

Evidências visuais reais em `.local/validation/locale/`, com cópia separada
dos perfis, sem alterar perfis/saves de uso normal:

| Execução | Evidência / limite |
|---|---|
| `ptbr-1` | Dashboard original com “início”, “Abrir Bandeja”; Guide com “Painel Xbox”, “Início do Xbox”. |
| `ptbr-restart` | Novo processo, mesmo diretório de teste: dashboard/Guide continuam em PT-BR. |
| `forza-ptbr` | Jogo real executado por 50 s, capturas internas; o mantenedor confirmou que o Forza ficou em português. Não houve aceite novo de campanha ou saves. |
| `en-br` | Inglês com país Brasil, comprovando a independência. Interação manual durante a execução alcançou as configurações originais e revelou o stub `XamGetCountry`; corrigido depois desta captura. |
| `ptbr-final` | Repetição focada após corrigir `XamGetCountry`; não é nova regressão geral. |
| `desktop-verified.png` | Desktop real: “Português (Brasil)” e “Brasil (BR)” carregados de `.local/console.toml`; 16 opções de idioma e 42 de país. |

O runner `scripts/validate-console-locale.ps1` limita o tempo e registra
argumentos, hashes, saída e capturas do presenter. O diagnóstico opt-in
`dash17559_guide_probe_seconds` solicita o serviço original do Guide uma vez,
sem mouse/teclado. Desligado, não adiciona consultas ao relógio no frame.
O desktop tem `--capture-settings <png>` apenas no build de desenvolvimento:
mostra a janela sem ativá-la, renderiza seus próprios controles após carregar
as preferências e termina. O arquivo `.state.txt` registra as contagens e
seleções dos controles, sem depender da leitura visual.

Os processos de jogo foram encerrados pelo limite diagnóstico: **não foi
certificado encerramento normal nem um ciclo visual completo de handoff**.
O primeiro `x360n run` foi recusado por recibo de preparação anterior
incompleto; não se removeu essa guarda nem se fabricou recibo. Estes builds
incrementais ainda precisam passar pela preparação normal para o lançamento
de produção. A validação visual foi diagnóstica, com as guardas dos originais
ativas. Sem Computer Use, push ou Release; sem alegação de ganho de performance.

Falhas durante o trabalho: o primeiro build do teste exigiu incluir `<array>`;
o primeiro teste de processos comparava LF com CRLF do fixture e foi corrigido
para saída binária determinística. A primeira autocaptura do desktop oculto
ficou preta; a captura passou a usar o pintor e controles do próprio aplicativo.
Os seletores nativos exigiram a janela visível para a autocaptura de seu texto;
o resultado final foi inspecionado com ambos os valores corretos.
Essas evidências iniciais foram preservadas.
