<?php
declare(strict_types=1);

// Unified testing endpoint for 3 firmwares:
// - Weather Monitoring: wind* + rain* + suhu/humidity/pressure + lidar values
// - Water Flow: enc_* values
// - Water Direction: ang_* values
// All use HTTP GET + Basic Auth.

header('Content-Type: text/plain; charset=utf-8');

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'GET') {
    http_response_code(405);
    echo "Method Not Allowed\n";
    exit;
}

define('AUTH_USER', getenv('WBB_AUTH_USER') ?: 'YOUR_HTTP_USER');
define('AUTH_PASS', getenv('WBB_AUTH_PASS') ?: 'YOUR_HTTP_PASSWORD');

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
    header('WWW-Authenticate: Basic realm="multisensoris"');
    http_response_code(401);
    echo "Unauthorized\n";
    exit;
}

function fail400(string $message): void {
    http_response_code(400);
    echo $message . "\n";
    exit;
}

/** @return string */
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

/** @return float */
function parseFloatOrFail(string $key, string $value): float {
    $value = str_replace(',', '.', $value);
    $f = filter_var($value, FILTER_VALIDATE_FLOAT);
    if ($f === false) {
        fail400("Invalid float: {$key}");
    }
    return (float)$f;
}

/** @return int */
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

$hasWeather = isset($_GET['windir']) || isset($_GET['windavg']) || isset($_GET['waveheight']);
$hasFlow = isset($_GET['enc_cps']) || isset($_GET['enc_dir']) || isset($_GET['enc_dt_ms']);
$hasDirection = isset($_GET['ang_angle']) || isset($_GET['ang_dir']) || isset($_GET['ang_dt_ms']);

$detected = array_filter([
    'weather' => $hasWeather,
    'water_flow' => $hasFlow,
    'water_direction' => $hasDirection,
]);

if (count($detected) !== 1) {
    fail400('Unknown/ambiguous payload (expected exactly one of: weather wind*, enc_*, ang_*)');
}

$type = array_key_first($detected);

$baseDir = __DIR__ . DIRECTORY_SEPARATOR . 'data';
if (!is_dir($baseDir)) {
    if (!mkdir($baseDir, 0775, true) && !is_dir($baseDir)) {
        http_response_code(500);
        echo "Server error: cannot create data dir\n";
        exit;
    }
}

$common = [
    'ts' => gmdate('c'),
    'ip' => $_SERVER['REMOTE_ADDR'] ?? '',
    'ua' => $_SERVER['HTTP_USER_AGENT'] ?? '',
    'type' => $type,
];

if ($type === 'weather') {
    $raw = [
        'windir' => requireQueryParam('windir'),
        'windavg' => requireQueryParam('windavg'),
        'windmax' => requireQueryParam('windmax'),
        'rain1h' => requireQueryParam('rain1h'),
        'rain24h' => requireQueryParam('rain24h'),
        'suhu' => requireQueryParam('suhu'),
        'humidity' => requireQueryParam('humidity'),
        'pressure' => requireQueryParam('pressure'),
        'distance' => requireQueryParam('distance'),
        'waterheight' => requireQueryParam('waterheight'),
        'waveheight' => requireQueryParam('waveheight'),
    ];

    $record = $common + [
        'windir' => parseIntOrFail('windir', $raw['windir']),
        'windavg' => parseFloatOrFail('windavg', $raw['windavg']),
        'windmax' => parseFloatOrFail('windmax', $raw['windmax']),
        'rain1h' => parseFloatOrFail('rain1h', $raw['rain1h']),
        'rain24h' => parseFloatOrFail('rain24h', $raw['rain24h']),
        'suhu' => parseFloatOrFail('suhu', $raw['suhu']),
        'humidity' => parseFloatOrFail('humidity', $raw['humidity']),
        'pressure' => parseFloatOrFail('pressure', $raw['pressure']),
        'distance' => parseIntOrFail('distance', $raw['distance']),
        'waterheight' => parseIntOrFail('waterheight', $raw['waterheight']),
        'waveheight' => parseIntOrFail('waveheight', $raw['waveheight']),
    ];

    $csvPath = $baseDir . DIRECTORY_SEPARATOR . 'weather_monitoring.csv';

} elseif ($type === 'water_flow') {
    $raw = [
        'enc_cps' => requireQueryParam('enc_cps'),
        'enc_rps' => requireQueryParam('enc_rps'),
        'enc_rpm' => requireQueryParam('enc_rpm'),
        'enc_dir' => requireQueryParam('enc_dir'),
        'enc_dcount' => requireQueryParam('enc_dcount'),
        'enc_total' => requireQueryParam('enc_total'),
        'enc_index' => requireQueryParam('enc_index'),
        'enc_dt_ms' => requireQueryParam('enc_dt_ms'),
    ];

    $record = $common + [
        'enc_cps' => parseFloatOrFail('enc_cps', $raw['enc_cps']),
        'enc_rps' => parseFloatOrFail('enc_rps', $raw['enc_rps']),
        'enc_rpm' => parseFloatOrFail('enc_rpm', $raw['enc_rpm']),
        'enc_dir' => parseIntOrFail('enc_dir', $raw['enc_dir']),
        'enc_dcount' => parseIntOrFail('enc_dcount', $raw['enc_dcount']),
        'enc_total' => parseIntOrFail('enc_total', $raw['enc_total']),
        'enc_index' => parseIntOrFail('enc_index', $raw['enc_index']),
        'enc_dt_ms' => parseIntOrFail('enc_dt_ms', $raw['enc_dt_ms']),
    ];

    $csvPath = $baseDir . DIRECTORY_SEPARATOR . 'water_flow.csv';

} else { // water_direction
    $raw = [
        'ang_angle' => requireQueryParam('ang_angle'),
        'ang_dir' => requireQueryParam('ang_dir'),
        'ang_dcount' => requireQueryParam('ang_dcount'),
        'ang_total' => requireQueryParam('ang_total'),
        'ang_cps' => requireQueryParam('ang_cps'),
        'ang_rps' => requireQueryParam('ang_rps'),
        'ang_rpm' => requireQueryParam('ang_rpm'),
        'ang_code' => requireQueryParam('ang_code'),
        'ang_count' => requireQueryParam('ang_count'),
        'ang_dt_ms' => requireQueryParam('ang_dt_ms'),
    ];

    $record = $common + [
        'ang_angle' => parseFloatOrFail('ang_angle', $raw['ang_angle']),
        'ang_dir' => parseIntOrFail('ang_dir', $raw['ang_dir']),
        'ang_dcount' => parseIntOrFail('ang_dcount', $raw['ang_dcount']),
        'ang_total' => parseIntOrFail('ang_total', $raw['ang_total']),
        'ang_cps' => parseFloatOrFail('ang_cps', $raw['ang_cps']),
        'ang_rps' => parseFloatOrFail('ang_rps', $raw['ang_rps']),
        'ang_rpm' => parseFloatOrFail('ang_rpm', $raw['ang_rpm']),
        'ang_code' => parseIntOrFail('ang_code', $raw['ang_code']),
        'ang_count' => parseIntOrFail('ang_count', $raw['ang_count']),
        'ang_dt_ms' => parseIntOrFail('ang_dt_ms', $raw['ang_dt_ms']),
    ];

    $csvPath = $baseDir . DIRECTORY_SEPARATOR . 'water_direction.csv';
}

$fileExisted = file_exists($csvPath);
$fp = fopen($csvPath, 'ab');
if ($fp === false) {
    http_response_code(500);
    echo "Server error: cannot open log\n";
    exit;
}

if (!flock($fp, LOCK_EX)) {
    fclose($fp);
    http_response_code(500);
    echo "Server error: cannot lock log\n";
    exit;
}

try {
    $needHeader = (!$fileExisted) || (filesize($csvPath) === 0);
    if ($needHeader) {
        fputcsv($fp, array_keys($record));
    }
    fputcsv($fp, array_values($record));
    fflush($fp);
} finally {
    flock($fp, LOCK_UN);
    fclose($fp);
}

http_response_code(200);
echo "OK\n";
