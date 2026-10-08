"use client";

import { useEffect, useState } from "react";

const INSTALLER_URL = "/api/download?artifact=installer";
const PORTABLE_URL = "/api/download?artifact=portable";
const REPOSITORY_URL = "https://github.com/andrewkernel/klip";

const navigation = [
  { id: "features", label: "features" },
  { id: "how-it-works", label: "how it works" },
  { id: "download", label: "download" },
  { id: "changelog", label: "changelog" },
];

const featureHighlights = [
  ["01", "instant replay", "save recent gameplay with your own shortcut; memory limits may shorten clips"],
  ["02", "full recording", "record complete sessions with a global keybind"],
  ["03", "audio control", "balance desktop and microphone levels before capture"],
  ["04", "hardware encode", "choose a supported hardware encoder and 60 or 120 fps; compatibility varies by GPU"],
];

const steps = [
  {
    number: "01",
    title: "Choose your source",
    copy: "Select a game, window, or full display. Klip remembers your source for next time.",
    detail: "Game / Window  ·  Display",
  },
  {
    number: "02",
    title: "Set your sound",
    copy: "Turn desktop audio and microphone capture on or off, then adjust each level from the dashboard.",
    detail: "Desktop  ·  Microphone",
  },
  {
    number: "03",
    title: "Capture the moment",
    copy: "Save the replay buffer with Alt + C or start a full recording with Alt + R.",
    detail: "Alt + C  ·  Alt + R",
  },
];

export default function Home() {
  const [activeSection, setActiveSection] = useState("features");
  const [menuOpen, setMenuOpen] = useState(false);

  useEffect(() => {
    const revealObserver = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) entry.target.classList.add("is-visible");
        });
      },
      { threshold: 0.14 },
    );

    const sectionObserver = new IntersectionObserver(
      (entries) => {
        const visible = entries
          .filter((entry) => entry.isIntersecting)
          .sort((a, b) => b.intersectionRatio - a.intersectionRatio)[0];
        if (visible?.target.id) setActiveSection(visible.target.id);
      },
      { rootMargin: "-35% 0px -50%", threshold: [0, 0.25, 0.6] },
    );

    document.querySelectorAll(".reveal").forEach((node) => revealObserver.observe(node));
    navigation.forEach(({ id }) => {
      const section = document.getElementById(id);
      if (section) sectionObserver.observe(section);
    });

    return () => {
      revealObserver.disconnect();
      sectionObserver.disconnect();
    };
  }, []);

  const closeMenu = () => setMenuOpen(false);

  return (
    <>
      <a className="skip-link" href="#main-content">
        Skip to content
      </a>

      <header className="site-header">
        <a className="brand" href="#top" aria-label="Klip home" onClick={closeMenu}>
          <img src="/favicon.ico" alt="" width="42" height="42" />
          <span>Klip</span>
        </a>

        <button
          className="menu-toggle"
          type="button"
          aria-label="Toggle navigation"
          aria-expanded={menuOpen}
          onClick={() => setMenuOpen((open) => !open)}
        >
          <span />
          <span />
        </button>

        <nav className={menuOpen ? "nav-links nav-links--open" : "nav-links"} aria-label="Main navigation">
          {navigation.map(({ id, label }) => (
            <a
              key={id}
              href={`#${id}`}
              className={activeSection === id ? "is-active" : ""}
              onClick={closeMenu}
            >
              {label}
            </a>
          ))}
          <a href={REPOSITORY_URL} target="_blank" rel="noreferrer" onClick={closeMenu}>
            source <span aria-hidden="true">↗</span>
          </a>
        </nav>

        <a className="button button--small header-download" href={INSTALLER_URL}>
          download Klip
          <span aria-hidden="true">↓</span>
        </a>
      </header>

      <main id="main-content">
        <section className="hero" id="top">
          <div className="hero-backdrop" aria-hidden="true" />
          <div className="hero-content">
            <p className="eyebrow hero-eyebrow"><span /> Klip for Windows</p>
            <h1>low-latency game clipping.</h1>
            <p className="hero-copy">Capture the moment before you miss it.</p>
            <a className="button button--primary hero-button" href={INSTALLER_URL}>
              <span className="download-icon" aria-hidden="true">↓</span>
              download Klip <strong>2.0 beta</strong>
            </a>
            <div className="hero-meta" aria-label="Product highlights">
              <span>Windows 10 &amp; 11</span>
              <span>Local recording</span>
              <span>No account</span>
            </div>
          </div>
          <a className="scroll-cue" href="#features" aria-label="Scroll to features">
            <span>Explore</span>
            <i aria-hidden="true">↓</i>
          </a>
        </section>

        <section className="section features-section product-features-section post-features-section" id="features">
          <div className="editorial-meta reveal" aria-hidden="true">
            <span>features</span>
            <span>[ built for Windows ]</span>
          </div>

          <div className="feature-showcase">
            <div className="feature-copy reveal">
              <p className="feature-kicker">one focused capture tool</p>
              <h2>the moments<br />matter. <span>the setup<br />doesn&rsquo;t.</span></h2>
              <p className="feature-lede">Klip 2.0 brings OBS-powered capture, replay, recording, and audio control into one focused dashboard. no scenes, browser sources, account, or cloud workflow to configure.</p>

              <div className="feature-list" aria-label="Klip features">
                {featureHighlights.map(([number, title, copy]) => (
                  <div className="feature-list-item" key={number}>
                    <span>[{number}]</span>
                    <strong>{title}</strong>
                    <p>{copy}</p>
                  </div>
                ))}
              </div>
            </div>

            <figure className="product-window reveal">
              <div className="product-window-bar">
                <span><i /> Klip / dashboard</span>
                <span>2.0 beta</span>
              </div>
              <img src="/klip-dashboard.png" alt="Klip dashboard showing replay, recording, capture source, and audio controls" />
              <figcaption>
                <span>actual application</span>
                <span>local capture · no account</span>
              </figcaption>
            </figure>
          </div>

          <div className="feature-footnote reveal">
            <span>a new capture foundation</span>
            <p>stock OBS game capture, hardware encoding, and compressed replay buffering replace the original capture engine. performance mode uses lighter encoder tuning, with an explicit quality tradeoff.</p>
            <a href="#download">download for Windows <span aria-hidden="true">↓</span></a>
          </div>
        </section>

        <section className="section workflow-section post-features-section" id="how-it-works">
          <div className="editorial-meta reveal" aria-hidden="true">
            <span>how it works</span>
            <span>[ three steps ]</span>
          </div>

          <div className="workflow-intro reveal">
            <h2>from game to clip,<br /><span>without the setup.</span></h2>
            <p>choose your source, set your sound, and let Klip quietly keep the last few minutes ready to save.</p>
          </div>

          <div className="steps" aria-label="how Klip works">
            {steps.map((step, index) => (
              <article className="step reveal" key={step.number} style={{ "--delay": `${index * 80}ms` } as React.CSSProperties}>
                <div className="step-number">[{step.number}]</div>
                <div className="step-content">
                  <p className="step-detail">{step.detail}</p>
                  <h3>{step.title}</h3>
                  <p>{step.copy}</p>
                </div>
                <span className="step-arrow" aria-hidden="true">↘</span>
              </article>
            ))}
          </div>
        </section>

        <section className="section download-section post-features-section" id="download">
          <div className="editorial-meta reveal" aria-hidden="true">
            <span>download / windows</span>
            <span>[ current: 2.0 beta ]</span>
          </div>

          <div className="download-panel reveal">
            <div className="release-pill"><span /> public beta</div>
            <h2>ready when<br />you are.</h2>
            <p className="download-lede">install Klip for Windows and start capturing locally. no account, no cloud, no subscription.</p>

            <div className="download-actions">
              <a className="button button--primary button--large" href={INSTALLER_URL}>
                <span className="download-icon" aria-hidden="true">↓</span>
                download installer
              </a>
              <a className="button button--secondary button--large" href={PORTABLE_URL}>
                portable zip <span aria-hidden="true">↗</span>
              </a>
              <a className="button button--secondary button--large" href={REPOSITORY_URL} target="_blank" rel="noreferrer">
                view source <span aria-hidden="true">↗</span>
              </a>
              <a href="https://github.com/andrewkernel/klip/releases/tag/v2.0.0">dependency sources + checksums ↗</a>
            </div>

            <div className="requirements">
              <span>Windows 10 1903+</span>
              <span>Windows 11</span>
              <span>x64</span>
              <span>GPL open source</span>
              <a href="https://aka.ms/vs/17/release/vc_redist.x64.exe">Microsoft VC++ x64 runtime</a>
            </div>

            <div className="unsigned-note">
              <span aria-hidden="true">!</span>
              <p><strong>public beta notice:</strong> this build is unsigned. verify its checksum before installing; do not disable Windows security. hardware and game compatibility vary, and broader testing is ongoing. clips and recordings save as MKV.</p>
            </div>
          </div>
        </section>

        <section className="section changelog-section post-features-section" id="changelog">
          <div className="section-inner changelog-layout">
            <div className="changelog-intro reveal">
              <div className="editorial-meta" aria-hidden="true">
                <span>changelog</span>
                <span>[ latest first ]</span>
              </div>
              <h2>what changed.</h2>
              <p>small, focused releases that make capture faster, clearer, and more dependable.</p>
              <a className="text-link" href="#download">
                get the latest build <span aria-hidden="true">↓</span>
              </a>
            </div>

            <article className="release-card reveal">
              <div className="release-card-header">
                <div>
                  <span className="release-version">Klip 2.0 / v2.0.0 beta</span>
                  <h3>a new OBS-powered capture engine</h3>
                </div>
                <time dateTime="2026-10-07">october 7, 2026</time>
              </div>
              <ul>
                <li><span>01 / engine</span><p>stock libobs handles game capture, display capture, audio, encoding, replay buffering, and recording</p></li>
                <li><span>02 / replay</span><p>save clips while recording, with bounded replay memory and clear duration-limit wording</p></li>
                <li><span>03 / performance</span><p>hidden and minimized dashboards stop rendering and computing visualization-only audio meters</p></li>
                <li><span>04 / control</span><p>explicit NVENC performance tuning and an optional game-capture copy limiter; quality tradeoffs are disclosed</p></li>
                <li><span>05 / files</span><p>MKV clips and recordings, customizable shortcuts, desktop/microphone volume controls, and separate audio tracks</p></li>
                <li><span>06 / beta</span><p>source and dependency build recipes are available. no universal FPS improvement or OBS-parity guarantee</p></li>
              </ul>
              <a href={INSTALLER_URL}>download Klip 2.0 <span aria-hidden="true">↓</span></a>
            </article>
          </div>
        </section>
      </main>

      <footer className="startup-footer">
        <div className="footer-callout reveal">
          <p>keep the moment.</p>
          <a href={INSTALLER_URL}>download Klip <span aria-hidden="true">↘</span></a>
        </div>
        <div className="footer-bottom">
          <a className="brand brand--footer" href="#top" aria-label="back to top">
            <img src="/favicon.ico" alt="" width="34" height="34" />
            <span>Klip</span>
          </a>
          <p>capture the moment before you miss it.</p>
          <div className="footer-links">
            <a href={REPOSITORY_URL} target="_blank" rel="noreferrer">source ↗</a>
            <a href="#changelog">changelog</a>
            <a href="/PRIVACY.md">privacy</a>
            <a href="/LICENSE.txt">license</a>
          </div>
        </div>
      </footer>
    </>
  );
}
