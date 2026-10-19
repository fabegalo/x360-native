# Intro original Retail 1888 — 18/10/2026

A animação original da **2.0.1888.0** renderizou a esfera, o logo Xbox 360 e
retornou à dashboard Blades. O mantenedor confirmou que o som original tocou
normalmente. Somente a 1888 foi executada, em Release, com armazenamento isolado
e timeout de 55 s. Sem jogos, outras versões, push ou publicação de Release.

## Arquivo e contrato demonstrado

Origem preservada: `.local/inputs/dashboards/1888/blades_retail_full_readme/bootanim.xex`;
cópia verificada em `1888/runtime/bootanim.xex`. SHA-256
`456e7a4ec249dec02cb39ae02729a6542307ff9bd9361e55afa3264bbb5ebcb3`,
397.312 bytes. A procedência é o acervo local descrito no
[relatório da dashboard](dashboard-1888-2026-10-18.md), sem autenticação
independente da assinatura Microsoft. Não foi usado o bootanim da 17559.

O cabeçalho identifica versão/base **2.0.1888.0**, imagem `0x98000000/0x8E000`,
sem entry point. O carregador informou filetime 2005-09-23 23:45:29 UTC.
Imagem carregada SHA-256 `d1e56fae488688c86ecf90be7079fa1d5c4c4544ceb0d90ff3dceac743cec748`.
Recurso `audio` em `0x98068100`, tamanho `0x25978`, magic RIFF, SHA-256
`4e7c725d9d7598b063d285ab4d0be5f6f04cab5e718fc567595cd511fc80adae`.
Imagem e recurso permanecem somente na evidência privada.

Disassembly e tabela de exports do próprio módulo:

- Export 1, `0x9801A010`: inicializa CRT, passa o hmodule a `0x980134C8`,
  executa a animação e faz cleanup. O hmodule permite ler o áudio embutido.
- Export 2, `0x9801A068`: `false` pede término gradual por objeto+`0x28`;
  `true` acionaria saída imediata por +`0x29`. A integração usa somente `false`.
- Export 3, `0x9801A088`: não chamado pela integração.
- Ponteiro inicial em `0x98042040` aponta para `0x98067600`. O inicializador
  `0x98011D50` apaga +`0x28` e +`0x29` em `0x98011D70/74`: pedir término antes
  dele perderia a solicitação. Esse contrato difere da integração histórica 17559.
- O controle original `0x98010358` exige tempo de animação >= **6 s**, efeito
  finalizado e hold >= **4 s** para a conclusão gradual; o limite da animação
  é **10 s**. Constantes lidas da própria imagem em `0x98000708/7B0/780`.

`boot_intro.cpp` envolve a entrada da dashboard, carrega somente o módulo
1888 identificado e exige os exports/mapeamentos esperados. `boot_intro_control.cpp`
é ligado à DLL bootanim da própria versão. Um `midasm_hook` depois do `bl` em
`0x98013524` chama export 2(false) após a inicialização. Não retira instruções,
desvia branches, escreve contadores ou substitui renderização/áudio. O contexto
PPC/FPSCR é preservado ao chamar o controle e ao iniciar a dashboard.

O hook de análise é necessário porque o AOT usa chamadas C++ diretas entre
essas funções internas; alterar somente a tabela do dispatcher não interceptaria
o inicializador. Timing, condições de término e cleanup continuam originais.

## Preparação e testes

Módulo próprio no manifesto e DLL `dash_1888_bootanim.dll` no inventário de
artefatos obrigatórios. O launcher inicia com `--dash1888_boot_intro=true`;
`skip_boot_intro_args` pertence somente à 1888 e usa `--dash1888_boot_intro=false`.
Retorno de dashboard não reinstala a intro. O início direto permanece diagnóstico;
nesta etapa não houve novo smoke sem intro nem ciclo com jogos.

Um tail call inicialmente não resolvido, `0x98038084 → 0x98034A58`, foi resolvido
por start comprovado após branch terminal. Mais 12 starts indiretos foram
conferidos com o checker existente e disassembly, somente na `.text`
`0x98008000–0x98041194`. Sem tamanhos inventados, `--force` ou patches binários.
A auditoria das **543 funções `.pdata`** geradas encontrou **zero overruns**.
Uma regeneração adicional manteve os hashes dos 14 arquivos do módulo, mas
terminou com erro de escrita em `generated/dash/codegen.d`; não foi contada como
geração completa aprovada. Para preservar a sessão aberta pelo mantenedor no
launcher, uma cópia diagnóstica do manifesto gerou outputs em outra pasta privada,
referenciando os mesmos originais/análises por caminhos absolutos: exit 0.
As **1.065 definições** coincidiram por nome e corpo C++, assim como os demais
metadados comparados; oito arquivos diferiram pela distribuição entre unidades
de tradução. Portanto, identidade byte a byte dos arquivos entre diretórios e
reprodutibilidade binária **não estão certificadas**. Não se alterou o SDK para
esconder essa diferença. Evidência: `intro/repro-comparison.json` e logs locais.
O aviso de unwind da dashboard `0x92164098 → 0x92164008` continua documentado;
não foi escondido por esta integração.

Build oficial: `scripts/devshell.cmd powershell -NoProfile -ExecutionPolicy Bypass
-File scripts/build-title.ps1 -Title dash_1888`. Logs privados
`x360n-build-*.2786e63b212e474a9d2239dbd764f5ad.log`: configure 8 s, ferramenta
97 s, codegen 8 s, reconfigure 8 s, native 74 s; todas as etapas exit 0.
LLVM 22.1.8, SDK `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, 91 patches do lock
preservados; Release `-O3`, `NDEBUG`, CRT Release e gates existentes mantidos.
Recibo publicado às 21:58 locais, sem marcador incomplete.

SHA-256 dos quatro artefatos desse recibo e da execução:

| Artefato | SHA-256 |
|---|---|
| `dash_1888.exe` | `14660e1075028c4349e2d5b6c01f387047c18f1263946fcb03b55c6d52b5c91d` |
| `dash_1888_bootanim.dll` | `4920579cca0e719c2ad9320b0578eb0fc7469f1a71a81c86fe929d025b1950c2` |
| `rexruntime.dll` | `f2f722f6029712431628d9b8cb14e2a4f2928b564d6634c90e736de4d15d2a98` |
| `rexgpu-xenos.dll` | `8f93b71e62c21510aae11ca6d588433b8065c1fdab84543eb0fc0287cecb8a47` |

Validações focadas:

- Diagnóstico existente `aot_module_registration_probe.cpp`, compilado em harness
  privado Release: **1.153 mapeamentos**, layout correspondente ao original,
  exports 1/2/3 e inicializador pertencentes à DLL própria. Não executa guest.
- Dependências/manifestos/isolamento: **2 casos / 200 asserções**, aprovados.
- Integration targets incluindo bootanim 1888: **1 caso / 74 asserções**, aprovado;
  aviso de Iruka ausente preservado. Binário do parser existente leu a TSV atual.
- Os **22 originais** foram reconferidos depois da execução; nenhuma origem alterada.
- Primeiro comando manual do codegen usou caminho com `/` que o `cmd` interpretou
  incorretamente; corrigido para caminho absoluto Windows. As chamadas diagnósticas
  dentro do sandbox não encerraram corretamente o shell e foram canceladas após
  inspeção dos logs; a geração final fora dele terminou com exit 0. Nenhum desses
  resultados iniciais foi usado como aprovação do build oficial.

## Execução observada e limites

Run `.local/evidence/runs/20261019T005843Z-dash1888-intro`, com Xenos, dados/cache/
metadata privados em `.local/evidence/dash1888/intro/smoke-20261019T005842Z`.
Idioma/país 1/1 somente no processo de diagnóstico, sem alterar a configuração
pt-BR persistente. O recurso localizado ausente continua sendo limitação do acervo.

Horários locais do log:

- **21:58:50.860:** início do export 1 original.
- **21:58:51.014:** inicialização concluída, export 2(false) solicitado.
- **21:59:02.611:** `VdPersistDisplay` reteve a imagem 1280×720.
- **21:59:02.613:** callback gráfico original da intro removido.
- **21:59:02.671:** áudio drenado antes de encerrar seu dispositivo.
- **21:59:02.727:** export 1 retornou, **11,867 s** após entrar; início da dashboard.
- **21:59:02.728:** callback gráfico original da dashboard instalado.

Capturas da saída real da GPU a cada dois segundos, ativadas somente no teste:
`captures/17923715282000963/frame-8.ppm` mostra a esfera em formação,
`frame-12.ppm` o logo Xbox 360, `frame-18.ppm` a dashboard Games legível.
14 frames foram convertidos para PNG sem mudar pixels, somente para inspeção.
Os caminhos são relativos à pasta privada do smoke; capturas não entram no Git.
O mantenedor respondeu **"Sim, o som tocou normalmente"**. Fidelidade exata da
mixagem, sincronismo audiovisual e comparação pixel a pixel ao console não foram
medidos.

O supervisor encerrou a dashboard por timeout em **55.213 ms**, sem crash
capturado, chamada AOT não registrada, falha de abertura ou erro crítico.
Isso não representa saída normal da dashboard; a intro, por sua vez, retornou
naturalmente antes do timeout. 14 linhas de stubs preexistentes, incluindo
`HalSendSMCMessage` e `XGetAudioFlags`; permanecem as duas mensagens XMP
`0x7002F/0x70044` da dashboard e XConfig SECURED `0x0001` não implementados.
Não foram adicionados stubs ou retornos fabricados. Serviços/perfis/Guide/jogos
e demais limites do relatório anterior continuam sem certificação nesta etapa.
