<?php
// Konfigurasi Database
$servername = getenv('WBB_DB_HOST') ?: 'localhost';
$username = getenv('WBB_DB_USER') ?: 'YOUR_DB_USER';
$password = getenv('WBB_DB_PASS') ?: 'YOUR_DB_PASSWORD';
$dbname = getenv('WBB_DB_NAME') ?: 'YOUR_DB_NAME';
$conn = new mysqli($servername, $username, $password, $dbname);

if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}
$conn->set_charset("utf8");

$distance1 = isset($_GET['distance1']) ? floatval($_GET['distance1']) : 0;
$distance2 = isset($_GET['distance2']) ? floatval($_GET['distance2']) : 0;
$rainfall = isset($_GET['rainfall']) ? floatval($_GET['rainfall']) : 0;
$rainfall_1h = isset($_GET['rainfall_1h']) ? floatval($_GET['rainfall_1h']) : 0;
$tip_count = isset($_GET['tip_count']) ? intval($_GET['tip_count']) : 0;

if ($distance1 < 0) $distance1 = 0;
if ($distance2 < 0) $distance2 = 0;
if ($rainfall < 0) $rainfall = 0;
if ($rainfall_1h < 0) $rainfall_1h = 0;
if ($tip_count < 0) $tip_count = 0;

$sql = "INSERT INTO esp1 (distance1, distance2, curah_hujan, curah_hujan_1h, jumlah_tip, waktu) 
        VALUES (?, ?, ?, ?, ?, NOW())";

$stmt = $conn->prepare($sql);
$stmt->bind_param("ddddi", $distance1, $distance2, $rainfall, $rainfall_1h, $tip_count);

if ($stmt->execute()) {
    echo "Data berhasil disimpan ke database dbpvwemonbaru - tabel esp1\n";
    echo "Distance1: " . $distance1 . " cm\n";
    echo "Distance2: " . $distance2 . " cm\n";
    echo "Curah Hujan (24h): " . $rainfall . " mm\n";
    echo "Curah Hujan (1h): " . $rainfall_1h . " mm\n";
    echo "Jumlah Tip: " . $tip_count . " tips\n";
    echo "Waktu: " . date('Y-m-d H:i:s') . "\n";
} else {
    echo "Error: " . $stmt->error;
}

$stmt->close();
$conn->close();
?>