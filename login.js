document.getElementById("loginForm").addEventListener("submit", function (e) {
  e.preventDefault();
  const username = document.getElementById("username").value;
  const password = document.getElementById("password").value;
  if (username === "admin" && password === "123456") {
    document.getElementById("message").style.color = "green";
    document.getElementById("message").textContent = "Đăng nhập thành công!";
    window.location.href = "home.html"; // Chuyển trang
  } else {
    document.getElementById("message").style.color = "red";
    document.getElementById("message").textContent =
      "Sai tên đăng nhập hoặc mật khẩu!";
  }
});
