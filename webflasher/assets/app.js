const compatibility = document.querySelector("#compatibility");
const compatibilityText = document.querySelector("#compatibility-text");
const hashElement = document.querySelector("#firmware-hash");
const copyButton = document.querySelector("#copy-hash");
const firmwareHash = "993B7EC8F9BB36B29F45A1BFAA269C68A1D55E10C078CFFB0CBF00829E321075";

if (window.isSecureContext && "serial" in navigator) {
  compatibility.classList.add("ready");
  compatibilityText.textContent = "Navigateur compatible — prêt à détecter l’ESP32";
} else {
  compatibility.classList.add("blocked");
  compatibilityText.textContent = window.isSecureContext
    ? "Utilisez Chrome ou Microsoft Edge sur ordinateur"
    : "Connexion HTTPS ou localhost requise";
}

hashElement.textContent = firmwareHash;
hashElement.title = firmwareHash;
document.querySelector("#year").textContent = `© ${new Date().getFullYear()}`;

copyButton.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(firmwareHash);
    copyButton.textContent = "Empreinte copiée";
    setTimeout(() => copyButton.textContent = "Copier l’empreinte", 1600);
  } catch {
    copyButton.textContent = "Copie impossible";
  }
});
