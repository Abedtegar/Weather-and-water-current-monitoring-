<?php
declare(strict_types=1);

// Weather Monitoring receiver endpoint
// Compatible with ESP32 firmware in Weather Monitoring/src/server.cpp:
// - Method: GET
// - Path: /multisensoris/kirimdata3.php
// - Basic Auth: <user> / <pass>
// - Query params: windir, windavg, windmax, rain1h, rain24h, suhu, humidity, pressure, distance, waterheight, waveheight

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

    if (!is_string($authHeader) || $authHeader === '') {
        return [null, null];
    }

    if (stripos($authHeader, 'Basic ') !== 0) {
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

/**
 * @param string $key
 * @return string
 */
function requireQueryParam(string $key): string {
    if (!isset($_GET[$key])) {
        http_response_code(400);
        echo "Missing param: {$key}\n";
        exit;
    }
    $value = $_GET[$key];
    if (is_array($value)) {
        http_response_code(400);
        echo "Invalid param: {$key}\n";
        exit;
    }
    return trim((string)$value);
}

/** @return float */
function parseFloatOrFail(string $key, string $value): float {
    $value = str_replace(',', '.', $value);
    $f = filter_var($value, FILTER_VALIDATE_FLOAT);
    if ($f === false) {
        http_response_code(400);
        echo "Invalid float: {$key}\n";
        exit;
    }
    return (float)$f;
}

/** @return int */
function parseIntOrFail(string $key, string $value): int {
    $i = filter_var($value, FILTER_VALIDATE_INT);
    if ($i === false) {
        http_response_code(400);
        echo "Invalid int: {$key}\n";
        exit;
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

$parsed = [
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

$record = [
    'ts' => gmdate('c'),
    'ip' => $_SERVER['REMOTE_ADDR'] ?? '',
    'ua' => $_SERVER['HTTP_USER_AGENT'] ?? '',
    'windir' => $parsed['windir'],
    'windavg' => $parsed['windavg'],
    'windmax' => $parsed['windmax'],
    'rain1h' => $parsed['rain1h'],
    'rain24h' => $parsed['rain24h'],
    'suhu' => $parsed['suhu'],
    'humidity' => $parsed['humidity'],
    'pressure' => $parsed['pressure'],
    'distance' => $parsed['distance'],
    'waterheight' => $parsed['waterheight'],
    'waveheight' => $parsed['waveheight'],
];

$baseDir = __DIR__ . DIRECTORY_SEPARATOR . 'data';
if (!is_dir($baseDir)) {
    if (!mkdir($baseDir, 0775, true) && !is_dir($baseDir)) {
        http_response_code(500);
        echo "Server error: cannot create data dir\n";
        exit;
    }
}

$csvPath = $baseDir . DIRECTORY_SEPARATOR . 'weather_monitoring.csv';
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
