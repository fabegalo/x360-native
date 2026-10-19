# Procedência do ícone

O ícone atual é geometria original criada para x360-native em 2026-10-18, para
acompanhar a identidade visual do gerenciador redesenhado em 2026-10-15: a mesma
marca da barra lateral, um X branco sobre o verde de destaque da paleta
(`RGB(16,124,16)`) em um quadrado de cantos arredondados (raio de um quarto do lado).
O gerador `scripts/packaging/generate-desktop-icon.ps1` contém toda a fonte: dois
traços retos cortados na horizontal e o quadrado arredondado, definidos por
coordenadas inteiras e cobertos por 4×4 amostras por pixel, desenhados diretamente
em cada tamanho, sem reamostragem.
Não usa fonte tipográfica, imagem, logotipo, modelo generativo ou recurso externo.
Não representa o símbolo Xbox nem afiliação com a Microsoft. O identificador do
recurso Windows permanece inalterado. Titular público confirmado pelo responsável:
**x360-native**. Licença: BSD-3-Clause, texto integral no `LICENSE` da raiz.

Gerar: `powershell -NoProfile -File scripts/packaging/generate-desktop-icon.ps1`.
Verificar sem modificar: acrescentar `-Check`. O ICO contém imagens DIB 32 bits
(alfa nos cantos) de 16, 20, 24, 32, 48, 64, 96, 128 e 256 pixels. Não depende de
fontes instaladas ou relógio da máquina. O inventário registra o hash do ICO; o
gate confere os bytes e o gerador. Medição local (2026-10-18, processo
`powershell -NoProfile` isolado, mesma máquina): `-Check` levou 5,9 s, contra
cerca de 1 s do gerador anterior de quatro tamanhos pixelados; o custo vem do
tamanho de 256 px e fica restrito ao gate de empacotamento.

O ícone intermediário, também geometria original x360-native (monograma XN
pixelado em fundo petróleo, 16–64 px), foi criado em 2026-10-13
(`d87b5c95f50fafd8058c8ce6f50b844cc238e75a`), SHA-256
`20c4f1e07fe0102cff771f1687c4acd128371fe1703f54b2b3b42e816c476e6a`, e foi
substituído porque não acompanhava mais a paleta da interface.

O recurso anterior a ele foi introduzido em
`fba55736988b3336fb7abc0bf069be475c2eb055`, blob
`b56e2d47a04a7c916b2b5aa95abeb29f576ac83f`, SHA-256
`31ca993c85b0896369797c1b270de184b1b6c6250a78b4c4fb65b4e9658648bd`.
O histórico disponível não documenta sua criação nem uma licença específica.
Ele foi substituído, sem afirmar autoria retroativa. Não integra o novo ZIP ou
seu arquivo de fontes. O mantenedor autorizou expressamente a publicação pública
do histórico existente, sem reescrita, para a primeira pre-release. O blob antigo
é preservado como registro histórico, sem alegar autoria ou licença retroativa;
a autorização não altera a procedência comprovada apenas dos ícones originais.
