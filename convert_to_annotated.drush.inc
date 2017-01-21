<?php

/**
 * Implements hook_drush_command().
 *
 * @return array
 */
function convert_to_annotated_drush_command() {
  $items = [];
  $items['convert-to-annotated'] = [
    'arguments' => ['Commandfile'],
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  ];
  return $items;
}

/**
 * Command validate.
 *
 * @param null $commandfile
 */
function drush_convert_to_annotated_validate($commandfile = NULL) {
  $all = drush_commandfile_list();
  if (!array_key_exists($commandfile, $all)) {
    drush_set_error('UNKNOWN_COMMANDFILE', dt('UNKNOWN_COMMANDFILE @file. Choose one of @files.', ['@file' => $commandfile, '@files' => implode(', ', array_keys($all))]));
  }
}

/**
 * Command callback.
 * @param $commandfile
 */
function drush_convert_to_annotated($commandfile) {
  $output = _drush_convert_to_annotated_get_header($commandfile);

  $commands = drush_get_commands();
  foreach ($commands as $name => $command) {
    if ($command['commandfile'] != $commandfile) {
      continue;
    }
    if ($name != 'pm-info') {continue;}
    var_dump($command);
    // Skip aliases.
    if ($name == $command['command']) {
      drush_log(dt('Generating @command annotation.', ['@command' => $name]), 'ok');
      $annotation = _drush_convert_to_annotated_get_annotation($command);

      drush_log(dt('Generating @command method.', ['@command' => $name]), 'ok');
      $method = _drush_convert_to_annotated_get_method($command);

      $output .= "\n" . $annotation . $method;

    }
  }

  $output .= '}'; // End of class.
  $filename = '/tmp/' . ucfirst($commandfile) . 'Commands.php';
  file_put_contents($filename, $output);

  drush_log(dt('Written to @filename.', ['@filename' => $filename]), 'status');
}

/**
 * File header.
 *
 * @param $commandfile
 * @return string
 */
function _drush_convert_to_annotated_get_header($commandfile) {
  $lines = [];
  $lines[] = '<?php';
  $lines[] = '';
  $lines[] = 'class ' . ucfirst($commandfile). 'Commands {';
  $lines[] = '';

  return implode("\n", $lines);
}

/**
 * Returns the corresponding bootstrap string.
 *
 * @param int $bootstrap
 * @return string
 */
function _drush_convert_to_annotated_get_bootstrap_string($bootstrap) {
  $levels = array_filter(get_defined_constants(), function ($key) {return strpos($key, "DRUSH_BOOTSTRAP_") === 0;}, ARRAY_FILTER_USE_KEY);
  $levels = array_flip($levels);

  return $levels[$bootstrap];
}

/**
 * Generates annotation from command definition.
 *
 * @param array $command
 *   Command definition
 * @return string
 */
function _drush_convert_to_annotated_get_annotation(array $command) {
  $lines = [];
  $lines[] = $command['description'];
  $lines[] = '';
  $lines[] = "@command ${command['command']}";
  $lines[] = '@aliases ' . implode(', ', $command['aliases']);
  $lines[] = "@bootstrap " . _drush_convert_to_annotated_get_bootstrap_string($command['bootstrap']);
  $lines[] = '';
  foreach ($command['arguments'] as $k => $v) {
    $lines[] = "@param string \$$k $v";
  }
  foreach ($command['options'] as $k => $v) {
    if (empty($v['hidden'])) {
      $lines[] = "@option \$$k ${v['description']}";
    }
  }
  // Complete callback.
  $complete_callback = 'drush_';
  if (function_exists($complete_callback)) {
    $lines[] = "@complete $complete_callback";
  }

  foreach ($command['examples'] as $k => $v) {
    $lines[] = "@usage $k";
    $lines[] = "  $v";
  }
  if (!empty($command['outputformat']['field-labels'])) {
    $lines[] = '@field-labels';
    foreach ($command['outputformat']['field-labels'] as $k => $v) {
      $lines[] = "  $k: $v";
    }
  }
  if (!empty($command['outputformat']['fields-default'])) {
    $lines[] = '@default-fields ' . implode(',', $command['outputformat']['fields-default']);
  }
  if (!empty($command['outputformat']['pipe-format'])) {
    $lines[] = '@pipe-format ' . $command['outputformat']['pipe-format'];
  }

  #* @table-style default
  #$lines[] = '';

  array_walk($lines, function(&$line) { $line = '   *' . (empty($line) ? '' : ' ' . $line); });

  return "  /**\n" . implode("\n", $lines) . "\n   */\n";
}

/**
 * Generates class method.
 *
 * @param $commandfile
 * @param $name
 * @param $command
 * @return string
 */
function _drush_convert_to_annotated_get_method($command) {
  $name = $command['command'];
  if (strpos($name, $command['commandfile'] . '-') === 0) {
    $name = substr($name, strlen($command['commandfile']) + 1);
  }
  $name = strtr($name, ['-' => '_']);

  $params = [];
  foreach ($command['arguments'] as $arg => $desc) {
    $params[] = "\$$arg = ''";
  }
  $options = [];
  foreach ($command['options'] as $opt => $desc) {
    $options[] = "'$opt' => ''";
  }
  if (!empty($options)) {
    $params[] = '$options = [' . implode(', ', $options) . ']';
  }

  $body = _drush_convert_to_annotated_get_callback_body($command);

  $method = [];
  $method[] = "  public function $name(" . implode(', ', $params) . ") {";
  $method[] = $body;
  $method[] = "  }";
  $method[] = '';

  return implode("\n", $method);
}

/**
 * Returns the code of the callback function.
 *
 * @param $f function name
 */
function _drush_convert_to_annotated_get_callback_body($command) {
  $output = '';

  // Include the external command file used by this command, if there is one.
  drush_command_include($command['command-hook']);

  // There's no API to obtain the command callback.
  // It is coupled in _drush_invoke_hooks().
  // Do here a minimum effort to find the hook.
  $callback = FALSE;
  $hook = str_replace("-", "_", $command['command-hook']);
  $candidates = [
    'drush_' . $command['commandfile'] . '_' . $hook,
    'drush_' . $hook,
  ];
  foreach ($candidates as $name) {
    if (function_exists($name)) {
      // http://stackoverflow.com/a/7027198/1319347
      $func = new ReflectionFunction($name);
      $filename = $func->getFileName();
      $start_line = $func->getStartLine();
      $end_line = $func->getEndLine() - 1;
      $length = $end_line - $start_line;

      $source = file($filename);
      $output = '  '. implode('  ', array_slice($source, $start_line, $length));

      break;
    }
  }

  return $output;
}
