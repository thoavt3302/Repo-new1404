from flask import Flask, request, render_template_string
import sqlite3
import os

app = Flask(__name__)

# Lỗi 1: Hardcoded secret key (CWE-798)
app.secret_key = "supersecretkey123"

# Lỗi 2: SQL Injection (CWE-89)
@app.route("/login", methods=["GET"])
def login():
    username = request.args.get("username")
    password = request.args.get("password")
    conn = sqlite3.connect("users.db")
    cursor = conn.cursor()
    
    # Truy vấn SQL không sử dụng parameterized query
    query = f"SELECT * FROM users WHERE username = '{username}' AND password = '{password}'"
    cursor.execute(query)  # ❌ SQL Injection!
    user = cursor.fetchone()
    conn.close()
    return "Logged in!" if user else "Failed!"

# Lỗi 3: XSS (Cross-Site Scripting) (CWE-79)
@app.route("/search")
def search():
    query = request.args.get("q", "")
    # Render trực tiếp input người dùng không được escape
    return render_template_string(f"<h1>Search Results for: {query}</h1>")  # ❌ XSS!

# Lỗi 4: Command Injection (CWE-78)
@app.route("/ping")
def ping():
    host = request.args.get("host", "8.8.8.8")
    # Thực thi lệnh hệ thống không validate input
    os.system(f"ping -c 1 {host}")  # ❌ Command Injection!
    return "Ping executed!"

# Lỗi 5: Debug mode enabled in production (CWE-215)
if __name__ == "__main__":
    app.run(debug=True)  # ❌ Debug mode nguy hiểm!

