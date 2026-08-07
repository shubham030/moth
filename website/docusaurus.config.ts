import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'moth',
  tagline: 'Write Dart. Run it on a microcontroller.',
  favicon: 'img/favicon.svg',

  url: 'https://shubham030.github.io',
  baseUrl: '/moth/',
  organizationName: 'shubham030',
  projectName: 'moth',
  trailingSlash: false,

  onBrokenLinks: 'warn',

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
          editUrl: 'https://github.com/shubham030/moth/tree/main/',
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
      logo: {alt: 'moth', src: 'img/moth.svg'},
      items: [
        {type: 'docSidebar', sidebarId: 'docs', position: 'left', label: 'Docs'},
        {to: '/docs/getting-started', label: 'Get started', position: 'left'},
        {to: '/docs/arduino-parity', label: 'Capabilities', position: 'left'},
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
            {label: 'Hardware capabilities', to: '/docs/arduino-parity'},
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
        'moth is MIT licensed. The API is unstable until v0.1 — build things, expect changes.',
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['dart', 'bash', 'c', 'cmake', 'yaml', 'ruby'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
