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

const TRACE = `$ mothc blink.dart
wrote blink.mothb (130 bytes)

$ mothrun blink.mothb --stop-after 2000
[     0ms] pin 38 -> output
[     0ms] pin 38 = HIGH
[   500ms] pin 38 = low
[  1000ms] pin 38 = HIGH
[  1500ms] pin 38 = low`;

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
  const logo = useBaseUrl('img/moth.svg');
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
              to="/docs/arduino-parity">
              What works today
            </Link>
          </div>
          <p className="heroNote">
            Runs on ESP32-S3 and ESP32-P4 · MIT licensed · v0.x, API unstable
          </p>
        </div>

        <div className="showcase">
          <Pane label="blink.dart" language="dart" dotColor="#7aa2f7">
            {BLINK}
          </Pane>
          <Pane label="your terminal — no hardware attached" language="bash" dotColor="#9ece6a">
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
    body: 'digitalWrite, analogRead, PWM, tone, millis, I2C, UART — the names match Arduino, so any tutorial translates line for line.',
  },
  {
    icon: '📦',
    title: 'Programs are 130 bytes',
    body: 'Your app is bytecode, not a firmware image. Push a change over the USB cable and it is on the screen in well under a second — no reflashing. Paired WiFi pushes add ~2s deriving the pairing key (or cache it in MOTH_PUSH_KEY).',
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

function Status() {
  return (
    <section className="section sectionAlt">
      <div className="container">
        <h2 className="sectionTitle">Honest status</h2>
        <p className="sectionLede">
          moth is early. Everything in the left column has an executable test
          behind it; everything on the right is genuinely not built yet.
        </p>
        <div className="statusGrid">
          <div className="statusCol">
            <h3>Working today</h3>
            <ul className="statusList">
              <li className="tick">Strings, lists, classes, closures, garbage collection</li>
              <li className="tick">Flutter-named widgets, setState, sliders and switches — 38fps on a round AMOLED</li>
              <li className="tick">Hot push over the USB cable or WiFi — no reflashing</li>
              <li className="tick">Digital and analog I/O, PWM, tone, I2C, UART</li>
              <li className="tick">Desktop simulator, golden tests, paint-cost budgets</li>
              <li className="tick">ESP32-S3 and ESP32-P4</li>
            </ul>
          </div>
          <div className="statusCol">
            <h3>Not yet</h3>
            <ul className="statusList">
              <li className="pending">Interrupts and async</li>
              <li className="pending">Enums, local captures, method tear-offs</li>
              <li className="pending">Images</li>
              <li className="pending">Scalable text and gradients</li>
              <li className="pending">State-preserving hot reload (push restarts the program)</li>
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
          Clone it, build the VM, and run a program — the first three steps need
          no board at all.
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
        <Status />
        <Closer />
      </main>
    </Layout>
  );
}
