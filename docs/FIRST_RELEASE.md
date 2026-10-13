# Primeira Release — v0.1.0-preview.19

**x360-native v0.1.0-preview.19 — First Technical Preview**, Windows x64.
Publicação pública expressamente autorizada pelo mantenedor em 13/10/2026,
substituindo a orientação anterior de manter o repositório privado.

- Repositório: https://github.com/fabegalo/x360-native
- Release: https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.19
- Tag: `v0.1.0-preview.19`, apontando para o commit usado no pacote.
- ZIP: `x360-native-0.1.0-preview.19-windows-x64.zip`.
- Checksum: `x360-native-0.1.0-preview.19-windows-x64.zip.sha256`.
- Notas: [First Technical Preview](releases/0.1.0-preview.19.md).

A identidade exata do código está em `package.json` dentro do ZIP e na tag.
O SHA-256 definitivo está no asset `.sha256` e nas notas publicadas. Esta
referência evita inserir no próprio payload o hash do ZIP que o contém.
Os links designam o destino autorizado; a publicação só é considerada concluída
após confirmação dos metadados e download anônimo dos assets no GitHub.

## Implementação encerrada e escopo

C1/C3, C2, C5, C6, C9, C12, C4/C11 e C10 estão tecnicamente concluídos.
A autorização do mantenedor encerra os antigos portões de planejamento desta
prévia; não remove verificações de integridade, avisos, créditos ou condições
legais aplicáveis a Microsoft e demais terceiros. O SDK completo, ferramentas
GPL/LGPL, jogos, dashboards, XAM, fontes Xbox, firmware e código AOT proprietário
não são redistribuídos nos assets. Ferramentas e SDK necessários à preparação
são obtidos separadamente, com consentimento e sob suas respectivas condições.

O novo ícone tem procedência e gerador documentados em `ICON_PROVENANCE.md`.
O mantenedor autorizou tornar público o histórico existente, incluindo o registro
do ícone anterior, sem reescrita. Não se atribui autoria/licença retroativa ao
blob antigo; a decisão de publicação não constitui parecer jurídico ou direitos
sobre conteúdo de terceiros. Evidências e obrigações permanecem em
`LICENSE_AUDIT.md` e `THIRD_PARTY_NOTICES.md`.

## Gates da entrega

O processo existente exporta o commit final e dependências fixadas, aplica os
38 patches verificados e produz builds desktop-only Release isoladas. O ZIP
contém a GUI, o supervisor, o extrator e fontes/scripts/configuração de preparação,
com licenças, avisos e índices de integridade. Nenhuma alteração de runtime,
SDK, dashboards, avatares ou jogos faz parte desta entrega.

Para o artefato definitivo, a publicação exige: duas builds com ZIPs idênticos;
CTest e gates existentes aprovados; revisão/payload/hash consistentes; negativos
de licenciamento/conteúdo; instalação/atualização e recibos; GUI iniciada de uma
extração persistente fora do repositório. Os resultados definitivos e o hash
são registrados nas notas da Release. Evidências locais ficam em
`.local/release-public-preview19/`, sem serem incluídas no pacote.

A verificação pré-publicação cobre todos os objetos alcançáveis pelas refs que
serão públicas, incluindo blobs antigos, nomes de arquivos e mensagens de commit,
procurando credenciais, arquivos pessoais e conteúdo indevido. Não publica
backups, refs locais adicionais, caches ou objetos não alcançáveis, nem espelha
submódulos. Uma busca por padrões não é garantia universal de ausência de segredos.

## Limitações que continuam explícitas

**Windows limpo NÃO VALIDADO.** O teste da GUI no host de desenvolvimento não
comprova a instalação de Build Tools/Windows SDK ausentes nem a experiência completa
para leigos. Essa validação é trabalho futuro, não novo portão desta prévia.
Compatibilidade permanece por versão exata; gameplay prolongado, save/load,
perfis/Guide completos e serviços online não são certificados. Nenhum jogo é
anunciado como Playable ou Fully Functional. A falha preexistente
`unit.Depfile escapes characters make treats as syntax` da suíte geral continua
documentada; esta entrega usa testes pertinentes e não recompila o catálogo.

O candidato anterior de `66947dc` foi validado localmente, mas seu ZIP/hash não
identificam esta Release. O pacote definitivo é reconstruído depois dos commits
de documentação e metadados; as alterações alheias de `window-route.ps1` e do
checkout SDK são preservadas e não entram no export do commit.
