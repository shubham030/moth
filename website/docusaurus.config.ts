import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'moth',
  tagline: 'Write Dart. Run it on a microcontroller.',
  favicon: 'img/favicon.png',

  url: 'https://shubham030.github.io',
  baseUrl: '/moth/',
  organizationName: 'shubham030',
  projectName: 'moth',
  trailingSlash: false,

  onBrokenLinks: 'throw',
  onBrokenAnchors: 'throw',

  i18n: {defaultLocale: 'en', locales: ['en']},

  // .md stays CommonMark so the design docs keep rendering as written;
  // only .mdx gets JSX semantics.
  markdown: {
    format: 'detect',
    hooks: {onBrokenMarkdownLinks: 'warn'},
  },

  presets: [
    [
      'classic',
      {
        docs: {
          path: '../docs',
          routeBasePath: 'docs',
          sidebarPath: './sidebars.ts',
          // PERF_REVIEW is the internal render-review checklist, not a doc.
          exclude: ['PERF_REVIEW.md'],
          editUrl: ({docPath}) =>
            `https://github.com/shubham030/moth/tree/main/docs/${docPath}`,
        },
        blog: false,
        theme: {customCss: './src/css/custom.css'},
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/moth-social.png',
    colorMode: {defaultMode: 'dark', respectPrefersColorScheme: true},
    navbar: {
      title: 'moth',
      logo: {alt: 'moth', src: 'img/moth.png'},
      items: [
        {type: 'docSidebar', sidebarId: 'docs', position: 'left', label: 'Docs'},
        {to: '/docs/getting-started', label: 'Get started', position: 'left'},
        {to: '/docs/arduino-parity', label: 'Hardware', position: 'left'},
        {
          href: 'https://github.com/shubham030/moth',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Learn',
          items: [
            {label: 'Getting started', to: '/docs/getting-started'},
            {label: 'The language', to: '/docs/language'},
            {label: 'Built-ins', to: '/docs/builtins'},
          ],
        },
        {
          title: 'Reference',
          items: [
            {label: 'Hardware', to: '/docs/arduino-parity'},
            {label: 'How it works', to: '/docs/how-it-works'},
            {label: 'Bytecode format', to: '/docs/bytecode'},
          ],
        },
        {
          title: 'Project',
          items: [
            {label: 'Roadmap', to: '/docs/roadmap'},
            {label: 'Prior art', to: '/docs/prior-art'},
            {label: 'GitHub', href: 'https://github.com/shubham030/moth'},
          ],
        },
      ],
      copyright:
        'moth is MIT licensed. v0.1 — the API will still move; pin your version.',
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['dart', 'bash', 'c', 'cmake', 'yaml', 'ruby'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
