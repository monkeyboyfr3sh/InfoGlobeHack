import { render } from 'preact';
import { App } from './app.jsx';
import './styles.css';

try {
  const t = localStorage.getItem('theme');
  if (t) document.documentElement.dataset.theme = t;
} catch { /* storage blocked */ }

render(<App />, document.getElementById('app'));
