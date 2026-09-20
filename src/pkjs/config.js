module.exports = [
  {
    type: 'heading',
    defaultValue: 'EasyCount beállítások'
  },
  {
    type: 'section',
    items: [
      {
        type: 'input',
        messageKey: 'ACTION_URL',
        label: 'URL',
        description: 'Az app ezt az URL-t hívja meg HTTP GET kéréssel.',
        defaultValue: '',
        attributes: {
          type: 'url',
          placeholder: 'https://example.com/action'
        }
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Mentés'
  }
];

