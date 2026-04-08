<?php
declare(strict_types=1);

header('Content-Type: text/plain; charset=utf-8');

$debug = (isset($_GET['debug']) && !is_array($_GET['debug']) && (string)$_GET['debug'] === '1');
$authorizedForDebug = false;

register_shutdown_function(function () use (&$authorizedForDebug, $debug): void {
    $err = error_get_last();
    if (!is_array($err) || !isset($err['type'])) {
        return;
    }

    $fatalTypes = [E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR];
    if (!in_array((int)$err['type'], $fatalTypes, true)) {
        return;
    }

    if (!headers_sent()) {
        http_response_code(500);
        header('Content-Type: text/plain; charset=utf-8');
    }
    echo "Server error\n";
    if ($debug && $authorizedForDebug) {
        $message = isset($err['message']) ? (string)$err['message'] : '';
        $file = isset($err['file']) ? (string)$err['file'] : '';
        $line = isset($err['line']) ? (int)$err['line'] : 0;
        if ($message !== '') {
            echo "Debug: {$message}\n";
        }
        if ($file !== '' && $line > 0) {
            echo "Debug: {$file}:{$line}\n";
        }
    }
});

$method = (string)($_SERVER['REQUEST_METHOD'] ?? 'GET');
if ($method !== 'GET' && $method !== 'POST') {
    http_response_code(405);
    echo "Method Not Allowed\n";
    exit;
}

define('AUTH_USER', getenv('WBB_AUTH_USER') ?: 'YOUR_HTTP_USER');
define('AUTH_PASS', getenv('WBB_AUTH_PASS') ?: 'YOUR_HTTP_PASSWORD');

// Installation constants (cm). Moved from firmware to server.
const SENSOR_TO_BOTTOM_CM = 1100;
const SENSOR_TO_CALM_WATER_CM = 400;

define('DB_HOST', getenv('WBB_DB_HOST') ?: 'localhost');
define('DB_USER', getenv('WBB_DB_USER') ?: 'YOUR_DB_USER');
define('DB_PASS', getenv('WBB_DB_PASS') ?: 'YOUR_DB_PASSWORD');
define('DB_NAME', getenv('WBB_DB_NAME') ?: 'Wemon_BauBau');

// Compatibility for older PHP runtimes.
if (!function_exists('hash_equals')) {
    /** @noinspection PhpMissingReturnTypeInspection */
    function hash_equals($known_string, $user_string) {
        $known = (string)$known_string;
        $user = (string)$user_string;
        $len = strlen($known);
        if ($len !== strlen($user)) {
            return false;
        }
        $res = 0;
        for ($i = 0; $i < $len; $i++) {
            $res |= ord($known[$i]) ^ ord($user[$i]);
        }
        return $res === 0;
    }
}

/** @return array{0:?string,1:?string} */
function getBasicAuthCredentials(): array {
    $user = $_SERVER['PHP_AUTH_USER'] ?? null;
    $pass = $_SERVER['PHP_AUTH_PW'] ?? null;
    if (is_string($user) && is_string($pass) && $user !== '') {
        return [$user, $pass];
    }

    $authHeader = null;
    foreach (['HTTP_AUTHORIZATION', 'REDIRECT_HTTP_AUTHORIZATION', 'Authorization'] as $key) {
        if (isset($_SERVER[$key]) && is_string($_SERVER[$key]) && $_SERVER[$key] !== '') {
            $authHeader = $_SERVER[$key];
            break;
        }
    }

    if (!is_string($authHeader) || $authHeader === '' || stripos($authHeader, 'Basic ') !== 0) {
        return [null, null];
    }

    $b64 = trim(substr($authHeader, 6));
    if ($b64 === '') {
        return [null, null];
    }

    $decoded = base64_decode($b64, true);
    if (!is_string($decoded)) {
        return [null, null];
    }

    $parts = explode(':', $decoded, 2);
    if (count($parts) !== 2) {
        return [null, null];
    }

    return [$parts[0], $parts[1]];
}

function requireBasicAuth(): void {
    header('WWW-Authenticate: Basic realm="Wemon_BauBau"');
    http_response_code(401);
    echo "Unauthorized\n";
    exit;
}

function fail400(string $message): void {
    http_response_code(400);
    echo $message . "\n";
    exit;
}

function fail500(string $message): void {
    http_response_code(500);
    echo $message . "\n";
    exit;
}

function requireQueryParam(string $key): string {
    if (!isset($_GET[$key])) {
        fail400("Missing param: {$key}");
    }
    $value = $_GET[$key];
    if (is_array($value)) {
        fail400("Invalid param: {$key}");
    }
    return trim((string)$value);
}

function optionalQueryParam(string $key): ?string {
    if (!isset($_GET[$key])) {
        return null;
    }
    $value = $_GET[$key];
    if (is_array($value)) {
        fail400("Invalid param: {$key}");
    }
    $trimmed = trim((string)$value);
    return $trimmed === '' ? null : $trimmed;
}

/** @return array{waterheight:int,waveheight:int} */
function computeHeightsFromDistance(int $distance): array {
    if ($distance < 0) {
        return ['waterheight' => -1, 'waveheight' => -1];
    }

    $waterheight = SENSOR_TO_BOTTOM_CM - $distance;
    if ($waterheight < 0) {
        $waterheight = 0;
    }

    // Keep firmware-compatible behavior: waveheight may be negative.
    $waveheight = SENSOR_TO_CALM_WATER_CM - $distance;
    return ['waterheight' => $waterheight, 'waveheight' => $waveheight];
}

function parseFloatOrFail(string $key, string $value): float {
    $value = str_replace(',', '.', $value);
    $f = filter_var($value, FILTER_VALIDATE_FLOAT);
    if ($f === false) {
        fail400("Invalid float: {$key}");
    }
    return (float)$f;
}

function parseIntOrFail(string $key, string $value): int {
    $i = filter_var($value, FILTER_VALIDATE_INT);
    if ($i === false) {
        fail400("Invalid int: {$key}");
    }
    return (int)$i;
}

[$user, $pass] = getBasicAuthCredentials();
if (!is_string($user) || !is_string($pass)) {
    requireBasicAuth();
}
if (!hash_equals(AUTH_USER, $user) || !hash_equals(AUTH_PASS, $pass)) {
    requireBasicAuth();
}

if ($debug) {
    $authorizedForDebug = true;
}

$type = null;
$json = null;

if ($method === 'GET') {
    $hasWeather = isset($_GET['windir']) || isset($_GET['windavg']) || isset($_GET['distance']);
    $hasFlow = isset($_GET['enc_rpm']) || isset($_GET['enc_cps']) || isset($_GET['enc_dir']) || isset($_GET['enc_dt_ms']);
    $hasDirection = isset($_GET['ang_angle']) || isset($_GET['ang_dir']) || isset($_GET['ang_dt_ms']);

    $detected = array_filter([
        'weather' => $hasWeather,
        'water_flow' => $hasFlow,
        'water_direction' => $hasDirection,
    ]);

    if (count($detected) !== 1) {
        fail400('Unknown/ambiguous payload (expected exactly one of: weather wind*, enc_*, ang_*)');
    }

    if (function_exists('array_key_first')) {
        $type = array_key_first($detected);
    } else {
        reset($detected);
        $type = key($detected);
    }
    if (!is_string($type) || $type === '') {
        fail400('Invalid payload');
    }
} else {
    $rawBody = file_get_contents('php://input');
    if (!is_string($rawBody) || trim($rawBody) === '') {
        fail400('Empty body');
    }

    $decoded = json_decode($rawBody, true);
    if (!is_array($decoded)) {
        fail400('Invalid JSON');
    }

    $json = $decoded;
    $typeVal = $json['type'] ?? null;
    if (!is_string($typeVal) || $typeVal === '') {
        fail400('Missing/invalid JSON field: type');
    }

    if ($typeVal === 'weather_lidar_distance_batch') {
        $type = 'weather_lidar_distance_batch';
    } else {
        fail400('Unknown JSON payload type');
    }
}

if (!class_exists('mysqli')) {
    fail500('Server error: mysqli extension not available');
}

$mysqli = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);
if ($mysqli->connect_error) {
    fail500('Server error: db connection failed');
}

if (!$mysqli->set_charset('utf8mb4')) {
    $mysqli->close();
    fail500('Server error: db charset failed');
}

if ($type === 'weather') {
    $windir = parseIntOrFail('windir', requireQueryParam('windir'));
    $windavg = parseFloatOrFail('windavg', requireQueryParam('windavg'));
    $windmax = parseFloatOrFail('windmax', requireQueryParam('windmax'));
    $rain1h = parseFloatOrFail('rain1h', requireQueryParam('rain1h'));
    $rain24h = parseFloatOrFail('rain24h', requireQueryParam('rain24h'));
    $suhu = parseFloatOrFail('suhu', requireQueryParam('suhu'));
    $humidity = parseFloatOrFail('humidity', requireQueryParam('humidity'));
    $pressure = parseFloatOrFail('pressure', requireQueryParam('pressure'));
    $distance = parseIntOrFail('distance', requireQueryParam('distance'));

    $waterheightStr = optionalQueryParam('waterheight');
    $waveheightStr = optionalQueryParam('waveheight');
    if ($waterheightStr === null || $waveheightStr === null) {
        $computed = computeHeightsFromDistance($distance);
        $waterheight = $waterheightStr === null ? $computed['waterheight'] : parseIntOrFail('waterheight', $waterheightStr);
        $waveheight = $waveheightStr === null ? $computed['waveheight'] : parseIntOrFail('waveheight', $waveheightStr);
    } else {
        $waterheight = parseIntOrFail('waterheight', $waterheightStr);
        $waveheight = parseIntOrFail('waveheight', $waveheightStr);
    }

    $stmt = $mysqli->prepare(
        'INSERT INTO weather_monitoring '
        . '(windir, windavg, windmax, rain1h, rain24h, suhu, humidity, pressure, distance, waterheight, waveheight) '
        . 'VALUES (?,?,?,?,?,?,?,?,?,?,?)'
    );
    if ($stmt === false) {
        $mysqli->close();
        fail500('Server error: db prepare failed');
    }

    if (!$stmt->bind_param(
        'idddddddiii',
        $windir,
        $windavg,
        $windmax,
        $rain1h,
        $rain24h,
        $suhu,
        $humidity,
        $pressure,
        $distance,
        $waterheight,
        $waveheight
    )) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db bind failed');
    }

    if (!$stmt->execute()) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db insert failed');
    }

    $stmt->close();

} elseif ($type === 'weather_lidar_distance_batch') {
    if (!is_array($json)) {
        $mysqli->close();
        fail400('Invalid JSON payload');
    }

    foreach (['windir', 'windavg', 'windmax', 'rain1h', 'rain24h', 'suhu', 'humidity', 'pressure', 'lidar_distance_cm'] as $key) {
        if (!array_key_exists($key, $json)) {
            $mysqli->close();
            fail400("Missing JSON field: {$key}");
        }
    }

    $windir = parseIntOrFail('windir', (string)$json['windir']);
    $windavg = parseFloatOrFail('windavg', (string)$json['windavg']);
    $windmax = parseFloatOrFail('windmax', (string)$json['windmax']);
    $rain1h = parseFloatOrFail('rain1h', (string)$json['rain1h']);
    $rain24h = parseFloatOrFail('rain24h', (string)$json['rain24h']);
    $suhu = parseFloatOrFail('suhu', (string)$json['suhu']);
    $humidity = parseFloatOrFail('humidity', (string)$json['humidity']);
    $pressure = parseFloatOrFail('pressure', (string)$json['pressure']);

    $sampleIntervalMs = 100;
    if (isset($json['sample_interval_ms'])) {
        $sampleIntervalMs = parseIntOrFail('sample_interval_ms', (string)$json['sample_interval_ms']);
    }

    $dist = $json['lidar_distance_cm'];
    if (!is_array($dist) || count($dist) !== 10) {
        $mysqli->close();
        fail400('Invalid JSON field: lidar_distance_cm (expected array length 10)');
    }

    $d01 = parseIntOrFail('lidar_distance_cm[0]', (string)$dist[0]);
    $d02 = parseIntOrFail('lidar_distance_cm[1]', (string)$dist[1]);
    $d03 = parseIntOrFail('lidar_distance_cm[2]', (string)$dist[2]);
    $d04 = parseIntOrFail('lidar_distance_cm[3]', (string)$dist[3]);
    $d05 = parseIntOrFail('lidar_distance_cm[4]', (string)$dist[4]);
    $d06 = parseIntOrFail('lidar_distance_cm[5]', (string)$dist[5]);
    $d07 = parseIntOrFail('lidar_distance_cm[6]', (string)$dist[6]);
    $d08 = parseIntOrFail('lidar_distance_cm[7]', (string)$dist[7]);
    $d09 = parseIntOrFail('lidar_distance_cm[8]', (string)$dist[8]);
    $d10 = parseIntOrFail('lidar_distance_cm[9]', (string)$dist[9]);

    // Keep legacy columns filled from the last sample (distance_10).
    $distance = $d10;
    $computed = computeHeightsFromDistance($distance);
    $waterheight = $computed['waterheight'];
    $waveheight = $computed['waveheight'];

    $stmt = $mysqli->prepare(
        'INSERT INTO weather_monitoring '
        . '(windir, windavg, windmax, rain1h, rain24h, suhu, humidity, pressure, distance, waterheight, waveheight, '
        . 'lidar_sample_interval_ms, '
        . 'distance_01, distance_02, distance_03, distance_04, distance_05, distance_06, distance_07, distance_08, distance_09, distance_10) '
        . 'VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)'
    );
    if ($stmt === false) {
        $mysqli->close();
        fail500('Server error: db prepare failed');
    }

    $types = 'i' . str_repeat('d', 7) . str_repeat('i', 14);

    if (!$stmt->bind_param(
        $types,
        $windir,
        $windavg,
        $windmax,
        $rain1h,
        $rain24h,
        $suhu,
        $humidity,
        $pressure,
        $distance,
        $waterheight,
        $waveheight,
        $sampleIntervalMs,
        $d01,
        $d02,
        $d03,
        $d04,
        $d05,
        $d06,
        $d07,
        $d08,
        $d09,
        $d10
    )) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db bind failed');
    }

    if (!$stmt->execute()) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db insert failed');
    }

    $stmt->close();

} elseif ($type === 'water_flow') {
    $enc_rpm = parseFloatOrFail('enc_rpm', requireQueryParam('enc_rpm'));

    $stmt = $mysqli->prepare(
        'INSERT INTO water_flow '
        . '(enc_rpm) '
        . 'VALUES (?)'
    );
    if ($stmt === false) {
        $mysqli->close();
        fail500('Server error: db prepare failed');
    }

    if (!$stmt->bind_param(
        'd',
        $enc_rpm
    )) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db bind failed');
    }

    if (!$stmt->execute()) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db insert failed');
    }

    $stmt->close();

} elseif ($type === 'water_direction') {
    $ang_dir = parseIntOrFail('ang_dir', requireQueryParam('ang_dir'));

    $stmt = $mysqli->prepare(
        'INSERT INTO water_direction '
        . '(ang_dir) '
        . 'VALUES (?)'
    );
    if ($stmt === false) {
        $mysqli->close();
        fail500('Server error: db prepare failed');
    }

    if (!$stmt->bind_param(
        'i',
        $ang_dir
    )) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db bind failed');
    }

    if (!$stmt->execute()) {
        $stmt->close();
        $mysqli->close();
        fail500('Server error: db insert failed');
    }

    $stmt->close();

} else {
    $mysqli->close();
    fail400('Invalid payload');
}

$mysqli->close();
http_response_code(200);
echo "OK\n";
