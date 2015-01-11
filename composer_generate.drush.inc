<?php

/**
 * Implements hook_drush_command().
 */
function composer_generate_drush_command() {
  $items = array();

  $items['composer-generate'] = array(
    'description' => "Drush command composer-generate.",
    // Describe the arguments for this command.  Delete
    // this seciton if command takes no arguments.
    //'arguments' => array(
    //  'arg' => 'Description of command argument.',
    //),
    // List all options used for this command. Only options that
    // are listed here (or that are global Drush options) may
    // be specified on the commnadline; all others raise an error.
    'options' => array(
      'base' => array(
        'description' => 'Path to composer.json or URL to git project containing base composer.json file.',
        'example-value' => 'https://raw.githubusercontent.com/derhasi/drupal-composer-project/master/composer.json',
      ),
      'stability' => array(
        'description' => 'Set minimum-stability',
        'example-value' => 'dev|alpha|beta|RC|stable',
      ),
    ),
    // Give one or more example commandline usages for this command
    'examples' => array(
      'drush composer-generate' => 'Create a composer.json file for an existing Drupal site that does not yet use composer.',
    ),
    /*
      // Delete the 'outputformat' record if command does not
      // produce any output.
      'outputformat' => array(
        'default' => 'table',
        'pipe-format' => 'var_export',
        'field-labels' => array(
          'lang' => 'Language',
          'msg' => 'Message',
        ),
        'fields-default' => array('lang', 'msg'),
        'output-data-type' => 'format-table',
      ),
    */
    'aliases' => array('compgen'),
  );

  // Commandfiles may also add topics.  These will appear in
  // the list of topics when `drush topic` is executed.
  $topic_file = dirname(__FILE__) . '/composer-generate-topic.txt';
  if (file_exists($topic_file)) {
    $items['docs-composer-generate'] = array(
      'description' => 'Description of command topic.',
      'hidden' => TRUE,
      'topic' => TRUE,
      'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
      'callback' => 'drush_print_file',
      'callback arguments' => array($topic_file),
    );
  }

  return $items;
}

/**
 * Implements hook_drush_help().
 *
 * @param
 *   A string with the help section (prepend with 'drush:')
 *
 * @return
 *   A string with the help text for your command.
 */
function composer_generate_drush_help($section) {
  switch ($section) {
    case 'drush:composer-generate':
      return dt("Brief help for Drush command composer-generate.");
    // The 'title' meta item is used to name a group of
    // commands in `drush help`.  If a title is not defined,
    // the default is "All commands in ___", with the
    // specific name of the commandfile (e.g. composer_generate).
    // Command files with less than four commands will
    // be placed in the "Other commands" section, _unless_
    // they define a title.  It is therefore preferable
    // to not define a title unless the file defines a lot
    // of commands.
    case 'meta:composer_generate:title':
      return dt("composer_generate commands");
    // The 'summary' meta item is displayed in `drush help --filter`,
    // and is used to give a general idea what the commands in this
    // command file do, and what they have in common.
    case 'meta:composer_generate:summary':
      return dt("Summary of all commands in this command group.");
  }
}

/**
 * Implementation of drush_hook_COMMAND().
 *
 * Main command implementation goes here.
 */
function drush_composer_generate() {
  // dev, alpha, beta, RC, and stable
  $minimum_stability = drush_get_option("stability", "stable");
  $base = drush_get_option('base', FALSE);

  $base_composer_json = _drush_composer_generate_get_base_file($base);

  $composer_data = json_decode($base_composer_json, TRUE);

  // Use the pm-updatestatus command to get info on the currently
  // installed projects.
  $values = drush_invoke_process("@self", "pm-updatestatus", array(), array(), array('integrate' => FALSE));
  $updatestatus_info = $values["object"];

  // Iterate over every project that was returned
  foreach ($updatestatus_info as $project_name => $project_info) {
    if
    (  ($project_name != 'drupal') &&
       ($project_info['project_status']) &&
       (isset($project_info['existing_version'])) &&
       (isset($project_info['link']))
    ) {
      $vendor_name = FALSE;
      $version = $project_info['existing_version'];
      $minimum_stability = _drush_composer_generate_auto_set_minimum_stability($version, $minimum_stability);
      // Convert a version from "7.x-3.1" to "7.3.1"
      $composer_version = str_replace('.x-', '.', $version);
      // At this point, we might have a version that looks something like one of:
      //   - 7.3.1
      //   - 7.1.0-beta2
      //   - 7.2.0-alpha3+108-dev
      // We are going to universally convert these to:
      //   - ~7.3
      // TODO: control this with an option, so folks can keep the
      // exact version numbers in their composer.json files if
      // they wish.  To do this, we should also determine which version
      // formats are acceptable in the Drupal packagist projects, and
      // force simplification of those which are not.
      $composer_version = preg_replace('/([0-9]*\.[0-9]*).*/', '${1}.*', $composer_version);
      // ~ syntax, if preferred.
      // $composer_version = preg_replace('/([0-9]*\.[0-9]*).*/', '~${1}', $composer_version);
      // Is this a project from drupal.org?
      if (strstr($project_info['link'], 'https://www.drupal.org/') !== FALSE) {
        $vendor_name = "drupal";
      }
      // TODO: try to find the $vendor_name of projects from other sources
      if ($vendor_name) {
        $composer_data['require']["$vendor_name/$project_name"] = $composer_version;
      }
    }
  }

  // If we really want this to work, we'd better also add composer_autoload
  if ((drush_drupal_major_version() < 8)  && (!array_key_exists('drupal/composer_vendor', $composer_data['require']))) {
    $composer_data['require']["drupal/composer_vendor"] = drush_drupal_major_version() . ".*";
  }

  // Insert "minimum stability: dev" if the user requested it.
  // TODO:  We might also want to auto-set this based on whether
  // there are dev version found when we are evaluating project versions
  // above.
  if ($minimum_stability != "stable") {
    $composer_data["minimum-stability"] = $minimum_stability;
    $composer_data["prefer-stable"] = TRUE;
  }
  // Produce the composer.json text from the $composer_data array
  $result_composer_json = json_encode($composer_data, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
  drush_print($result_composer_json);
}

function _drush_composer_generate_get_base_file($base) {
  $filespec = __DIR__ . '/templates/d' . drush_drupal_major_version() . '_composer.json';
  return file_get_contents($filespec);

/*
  // If not specified, then we'll use a default file crafted
  // for the current version of Drupal being used.
  if (empty($base)) {
  }
  // Is there a protocol?
  elseif(strstr($base, '://')) {

  }
  // Otherwise, presume a file
  else {

  }
*/
}

function _drush_composer_generate_auto_set_minimum_stability($version, $cur_minimum_stability) {
  foreach (array("dev", "alpha", "beta", "RC") as $check_stability) {
    // Don't check any stability level that is greater than the current minimum
    if ($check_stability == $cur_minimum_stability) {
      return $cur_minimum_stability;
    }
    // If we can find the current 'check stability' in the version
    // string we are testing, then set the minimum stability appropriately.
    if (strstr($version, '-' . $check_stability)) {
      return $check_stability;
    }
  }
  return $cur_minimum_stability;
}