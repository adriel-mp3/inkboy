# InkBoy — decisões do projeto

Resumo das decisões que orientam o estágio atual. Detalhes de investigação e
evidências ficam nos documentos de experimento.

## Produto e escopo

- InkBoy é uma aplicação C independente, não um frontend nem um fork de
  Gambatte-K2.
- O alvo é o Kindle Paperwhite 5 (11ª geração), firmware 5.19.2.
- O estágio atual exibe uma tela estática monocromática: o SVG original do
  pato e o texto `InkBoy`.
- Input, animação, emulação e menu ficam fora deste estágio.

## Renderização

- A renderização usa FBInk mínimo, compilado estaticamente dentro do
  executável.
- A aplicação usa a API pública do FBInk; não acessa `/dev/fb0` nem usa
  ioctls específicos do Kindle diretamente.
- O desenho acontece uma vez, seguido de um refresh explícito.
- O framebuffer anterior é salvo antes do desenho e restaurado em uma saída
  normal (`SIGINT`, `SIGTERM` ou `SIGHUP`).

## Dependências e build

- FBInk fica vendorizado no commit upstream `886f25f`, com apenas os recursos
  de imagem e desenho necessários.
- O SVG é convertido no host para um bitmap C; o Kindle não precisa de SVG,
  librsvg ou Cairo.
- A compilação continua usando o toolchain/SDK KindleHF, Docker e Make.
- O artefato é instalado somente em `/mnt/us/inkboy`; o root filesystem do
  Kindle não é alterado.

## Assets e licenças

- `assets/duck.svg` é a fonte visual oficial e não deve ser redesenhado ou
  substituído por um mascote inventado.
- O projeto usa GNU GPL-3.0-only. Avisos e licenças de terceiros devem ser
  mantidos e a compatibilidade verificada antes de reutilizar código.

## Decisões futuras

- FBInk só deve ser removido depois de uma alternativa ser validada no PW5
  físico, preservando refresh e restauração.
- Aumentar o escopo para emulação, input ou atualização contínua exige uma
  decisão e validação próprias; não é uma consequência automática deste POC.

Para o racional detalhado sobre FBInk, consulte
[`architecture.md`](architecture.md). Para a evidência do primeiro renderer,
consulte [`experiments/02-rendering.md`](experiments/02-rendering.md).
