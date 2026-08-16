import type {ReactNode} from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import CodeBlock from '@theme/CodeBlock';
import useBaseUrl from '@docusaurus/useBaseUrl';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';

const BLINK = `void main() {
  var led = 38;
  pinOutput(led);

  var on = false;
  while (true) {
    on = !on;
    digitalWrite(led, on);
    delay(500);
  }
}`;

const TRACE = `$ dart pub global activate mothc
$ moth run blink.dart
Launching blink.dart on /dev/cu.usbmodem21201

pushed in 174ms
r  hot restart (recompile + push; state resets)
h  this help   q  quit`;

function Pane({
  label,
  language,
  children,
  dotColor,
}: {
  label: string;
  language: string;
  children: string;
  dotColor: string;
}) {
  return (
    <div className="pane">
      <div className="paneHeader">
        <span className="dot" style={{background: dotColor}} />
        {label}
      </div>
      <CodeBlock language={language}>{children}</CodeBlock>
    </div>
  );
}

function Hero() {
  const logo = useBaseUrl('img/moth.png');
  return (
    <header className="heroBanner">
      <div className="heroInner">
        <div className="container">
          <img src={logo} alt="" className="heroLogo" />
          <h1 className="heroTitle">
            Write Dart.
            <br />
            <span className="heroAccent">Run it on a microcontroller.</span>
          </h1>
          <p className="heroSubtitle">
            moth compiles a subset of Dart to compact bytecode and interprets it
            on the chip. The same language you use for Flutter, now blinking an
            LED — no second toolchain, no C.
          </p>
          <div className="heroButtons">
            <Link className="button button--primary button--lg" to="/docs/getting-started">
              Get started
            </Link>
            <Link
              className="button button--secondary button--lg"
              to="/docs/roadmap">
              What works today
            </Link>
          </div>
          <p className="heroNote">
            Runs on ESP32-S3 · MIT licensed · v0.x, API unstable
          </p>
        </div>

        <div className="showcase">
          <Pane label="blink.dart" language="dart" dotColor="#7aa2f7">
            {BLINK}
          </Pane>
          <Pane label="your terminal — with no board it opens the simulator" language="bash" dotColor="#9ece6a">
            {TRACE}
          </Pane>
        </div>
      </div>
    </header>
  );
}

const FEATURES = [
  {
    icon: '🦋',
    title: 'One language, both worlds',
    body: 'If you are learning Dart for Flutter, the board on your desk speaks it too. The for loop you already know, on a chip that costs a few dollars.',
  },
  {
    icon: '⚡',
    title: 'Runs without hardware',
    body: 'The simulator drives pins, I2C and UART against a virtual clock, so three seconds of blinking finish instantly. Buy the board later.',
  },
  {
    icon: '🔌',
    title: 'Arduino parity',
    body: 'digitalWrite, analogRead, PWM, tone, millis, servos, I2C with bulk reads, UART, and preferences that survive a reboot — the names match Arduino, so any tutorial translates line for line.',
  },
  {
    icon: '📦',
    title: 'A program, not a firmware image',
    body: 'Blink is 138 bytes of bytecode. Push a change over the USB cable and it is on the screen in well under a second — no reflashing. Paired WiFi pushes add ~2s deriving the pairing key (or cache it in MOTH_PUSH_KEY).',
  },
];

function Features() {
  return (
    <section className="section">
      <div className="container">
        <h2 className="sectionTitle">Why moth</h2>
        <p className="sectionLede">
          Not a code generator and not a lookalike syntax — a real interpreter
          running your compiled Dart on the microcontroller itself.
        </p>
        <div className="cards">
          {FEATURES.map((f) => (
            <div className="card" key={f.title}>
              <div className="cardIcon">{f.icon}</div>
              <h3>{f.title}</h3>
              <p>{f.body}</p>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}

function Demo() {
  const run = useBaseUrl('video/moth-run.mp4');
  const touch = useBaseUrl('video/moth-touch.mp4');
  const videoStyle = {width: '100%', borderRadius: 12, display: 'block'};
  return (
    <section className="section">
      <div className="container">
        <h2 className="sectionTitle">See it run</h2>
        <p className="sectionLede">
          One unedited take: <code>moth run</code>, a device picker because two
          boards are plugged in, a color edit in the editor, and <code>r</code> —
          the panel is orange before the logs settle.
        </p>
        <video style={videoStyle} src={run} autoPlay loop muted playsInline />
        <p className="sectionLede" style={{marginTop: '2rem'}}>
          Touch is renderer-owned, so controls track a finger without the VM in
          the frame loop — a slider and a switch from{' '}
          <code>examples/ui/controls.dart</code>, pushed over the same running
          session:
        </p>
        <video style={videoStyle} src={touch} autoPlay loop muted playsInline />
      </div>
    </section>
  );
}

function Status() {
  return (
    <section className="section sectionAlt" id="status">
      <div className="container">
        <h2 className="sectionTitle">Honest status</h2>
        <p className="sectionLede">
          moth is early. Everything in the left column either runs in CI or was
          measured on the board; everything on the right is genuinely not built
          yet.
        </p>
        <div className="statusGrid">
          <div className="statusCol">
            <h3>Working today</h3>
            <ul className="statusList">
              <li className="tick">Strings, lists, classes, closures, garbage collection</li>
              <li className="tick">Flutter-named widgets, setState, sliders, switches, and embedded images — 38fps at 466x466, measured on the board</li>
              <li className="tick"><code>moth run</code> — picks up a connected board (or opens the simulator), streams its console, and <code>r</code> hot-restarts in ~173ms</li>
              <li className="tick"><code>moth create</code> scaffolds a project your editor understands — autocomplete and error-checking on every built-in</li>
              <li className="tick">Hot push over the USB cable or paired WiFi — no reflashing</li>
              <li className="tick">Digital and analog I/O, PWM, tone, servos, I2C with bulk reads and writes, UART, NVS-backed preferences</li>
              <li className="tick">Installs from pub: <code>dart pub global activate mothc</code></li>
              <li className="tick">Desktop simulator, golden tests, paint-cost budgets</li>
              <li className="tick">ESP32-S3 with the Waveshare 1.75&quot; round AMOLED — the verified board</li>
            </ul>
          </div>
          <div className="statusCol">
            <h3>Not yet</h3>
            <ul className="statusList">
              <li className="pending">Interrupts and async</li>
              <li className="pending">SPI</li>
              <li className="pending">Enums, mixins, extensions, and capturing locals in closures</li>
              <li className="pending">Scalable text and gradients</li>
              <li className="pending">Network and vector images — <code>Image</code> embeds PNG/JPEG at compile time only</li>
              <li className="pending">State-preserving hot reload — <code>r</code> is a hot restart (~173ms) and your state resets</li>
              <li className="pending">Boards beyond the one above — other ESP-IDF targets are ports, not rewrites, but none is built or verified</li>
            </ul>
          </div>
        </div>
      </div>
    </section>
  );
}

function Closer() {
  return (
    <section className="section">
      <div className="container" style={{textAlign: 'center'}}>
        <h2 className="sectionTitle">Blink something in five minutes</h2>
        <p className="sectionLede">
          <code>dart pub global activate mothc</code>, then{' '}
          <code>moth create</code> and <code>moth run</code> — a board is
          optional; without one it opens the simulator.
        </p>
        <div className="heroButtons">
          <Link className="button button--primary button--lg" to="/docs/getting-started">
            Read the guide
          </Link>
          <Link
            className="button button--secondary button--lg"
            href="https://github.com/shubham030/moth">
            View on GitHub
          </Link>
        </div>
      </div>
    </section>
  );
}

export default function Home(): ReactNode {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout title={siteConfig.tagline} description="Run Dart on ESP32 microcontrollers.">
      <Hero />
      <main>
        <Features />
        <Demo />
        <Status />
        <Closer />
      </main>
    </Layout>
  );
}
