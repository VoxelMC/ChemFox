// Light/dark toggle shared by every page (the class itself is set early in each page's <head>).
document.getElementById("theme").addEventListener("click", () => {
  const dark = document.documentElement.classList.toggle("dark");
  try { localStorage.setItem("theme", dark ? "dark" : "light"); } catch (e) {}
});
