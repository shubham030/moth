import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  docs: [
    {
      type: 'category',
      label: 'Learn',
      collapsed: false,
      items: ['getting-started', 'language', 'hardware', 'builtins', 'arduino-parity', 'testing'],
    },
    {
      type: 'category',
      label: 'Understand',
      collapsed: false,
      items: ['how-it-works', 'ARCHITECTURE', 'BYTECODE', 'BACKEND'],
    },
    {
      type: 'category',
      label: 'Project',
      collapsed: false,
      items: ['ROADMAP', 'PRIOR-ART', 'DECISIONS'],
    },
  ],
};

export default sidebars;
